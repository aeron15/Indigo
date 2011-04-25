/****************************************************************************
 * Copyright (C) 2009-2011 GGA Software Services LLC
 * 
 * This file is part of Indigo toolkit.
 * 
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 3 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 * 
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ***************************************************************************/

#include "oracle/bingo_storage.h"
#include "base_cpp/tlscont.h"
#include "oracle/ora_wrap.h"
#include "base_cpp/output.h"
#include "base_cpp/shmem.h"
#include "base_cpp/auto_ptr.h"

BingoStorage::BingoStorage (OracleEnv &env, int context_id)
{
   _shmem_state = 0;
   _top_lob = 0;
   _index_lob = 0;
   _age_loaded = -1;

   QS_DEF(Array<char>, instance);
   QS_DEF(Array<char>, schema);

   OracleStatement::executeSingleString(instance, env,
           "SELECT PROPERTY_VALUE from database_properties where property_name = 'GLOBAL_DB_NAME'");
   OracleStatement::executeSingleString(schema, env,
           "SELECT SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') from dual");

   ArrayOutput output1(_shmem_id);

   output1.printf("%s#%s#%d#bs2", instance.ptr(), schema.ptr(), context_id);
   output1.writeChar(0);

   ArrayOutput output2(_table_name);

   output2.printf("STORAGE_%d", context_id);
   output2.writeChar(0);
}

BingoStorage::~BingoStorage ()
{
   delete _shmem_state;
      
   delete _top_lob;
   delete _index_lob;
}

void BingoStorage::create (OracleEnv &env)
{
   const char *tn = _table_name.ptr();
   OracleStatement::executeSingle(env, "CREATE TABLE %s(id number, bindata BLOB) "
      "NOLOGGING lob(bindata) store as (CACHE READS NOLOGGING PCTVERSION 0)", tn);
   OracleStatement::executeSingle(env, "CREATE INDEX %s_id ON %s(id)", tn, tn);
   OracleStatement::executeSingle(env, "INSERT /*+ NOLOGGING */ INTO %s VALUES(0, EMPTY_BLOB())", tn);
}

void BingoStorage::drop (OracleEnv &env)
{
   OracleStatement::executeSingle(env, "BEGIN DropTable('%s'); END;", _table_name.ptr());
   delete _shmem_state;
   _shmem_state = 0;
   _age_loaded = -1;
}

void BingoStorage::truncate (OracleEnv &env)
{
   const char *tn = _table_name.ptr();

   OracleStatement::executeSingle(env, "TRUNCATE TABLE %s", tn);
   OracleStatement::executeSingle(env, "INSERT /*+ NOLOGGING */ INTO %s VALUES(0, EMPTY_BLOB())", tn);
   delete _shmem_state;
   _shmem_state = 0;
   _age_loaded = -1;
}

void BingoStorage::validateForInsert (OracleEnv &env)
{
   _blocks.clear();

   OracleStatement statement(env);

   int id, length;
   OracleLOB lob(env);

   statement.append("SELECT id, length(bindata), bindata FROM %s ORDER BY id",
      _table_name.ptr());

   statement.prepare();
   statement.defineIntByPos(1, &id);
   statement.defineIntByPos(2, &length);
   statement.defineBlobByPos(3, lob);
   statement.execute();

   _n_added = -1;
   do
   {
      if (id == 0)
      {
         if ((length % sizeof(_Addr)) != 0)
            throw Error("unexpected LOB size %d is not multiple of %d", length, sizeof(_Addr));
         _n_added = length / sizeof(_Addr);
         continue;
      }

      _Block &block = _blocks.push();

      block.size = length;
   } while (statement.fetch());

   if (_n_added < 0)
      throw Error("missing index LOB");

   if (_blocks.size() > 0)
      _top_lob = _getLob(env, _blocks.size());
   
   _index_lob = _getLob(env, 0);
}

void BingoStorage::validate (OracleEnv &env)
{
   env.dbgPrintfTS("validating storage... ");

   if (_shmem_state != 0 && strcmp(_shmem_state->getID(), _shmem_id.ptr()) != 0)
   {
      delete _shmem_state;
      _shmem_state = 0;
      _age_loaded = -1;
   }

   _State *state = _getState(true);

   // TODO: implement a semaphore
   while (state->state == _STATE_LOADING)
   {
      delete _shmem_state;
      _shmem_state = 0;
      _age_loaded = -1;
      
      state = _getState(true);

      if (state == 0)
         throw Error("can't get shared info");

      env.dbgPrintf(".");
   }

   if (state->state == _STATE_READY)
   {
      if (state->age_loaded == state->age)
      {
         if (_age_loaded == state->age)
         {
            env.dbgPrintf("up to date\n");
            return;
         }
         else
            env.dbgPrintf("loaded by the other process\n");
      }
      else
      {
         env.dbgPrintf("has changed, reloading\n");
         state->state = _STATE_LOADING;
      }
   }
   else
   {
      state->state = _STATE_LOADING;
      env.dbgPrintf("loading ... \n");
   }

   _shmem_array.clear();
   _blocks.clear();

   OracleStatement statement(env);

   int id, length;
   OracleLOB lob(env);
   QS_DEF(Array<char>, block_name);

   statement.append("SELECT id, length(bindata), bindata FROM %s ORDER BY id",
      _table_name.ptr());

   statement.prepare();
   statement.defineIntByPos(1, &id);
   statement.defineIntByPos(2, &length);
   statement.defineBlobByPos(3, lob);
   statement.execute();

   do
   {
      ArrayOutput output(block_name);
      output.printf("%s_%d_%d", _shmem_id.ptr(), id, state->age);
      output.writeByte(0);

      if (id == 0)
      {
         if ((length % sizeof(_Addr)) != 0)
            throw Error("unexpected LOB size %d is not a multiple of %d", length, sizeof(_Addr));
         _index.resize(length / sizeof(_Addr));
         if (length > 0)
            lob.read(0, (char *)_index.ptr(), length);
         continue;
      }

      if (length < 1)
         throw Error("cannot validate block #%d: length=%d", id, length);

      _shmem_array.add(new SharedMemory(block_name.ptr(), length, state->state == _STATE_READY));

      void *ptr = _shmem_array.top()->ptr();

      if (ptr == 0)
      {
         if (state->state == _STATE_READY)
         {
            // That's rare case, but possible.
            // Reload the storage.
            env.dbgPrintf("shared memory is gone, resetting... \n");
            state->state = _STATE_EMPTY;
            validate(env);
            return;
         }
         else
            throw Error("can't map block #%d", id);
      }

      if (state->state != _STATE_READY)
         lob.read(0, (char *)ptr, length);

      _Block &block = _blocks.push();

      block.size = length;
   } while (statement.fetch());

   state->state = _STATE_READY;
   state->age_loaded = state->age;
   _age_loaded = state->age;
}

OracleLOB * BingoStorage::_getLob (OracleEnv &env, int no)
{
   OracleStatement statement(env);
   AutoPtr<OracleLOB> lob(new OracleLOB(env));

   statement.append("SELECT bindata FROM %s where ID = :id FOR UPDATE", _table_name.ptr());
   statement.prepare();
   statement.bindIntByName(":id", &no);
   statement.defineBlobByPos(1, lob.ref());
   statement.execute();

   if (statement.fetch())
      env.dbgPrintf("WARNING: more than 1 row have id = %d in table %s\n", no, _table_name.ptr());

   lob->enableBuffering();
   return lob.release();
}

void BingoStorage::_insertLOB (OracleEnv &env, int no)
{
   if (_top_lob != 0)
   {
      env.dbgPrintf("ending storage LOB\n");
      delete _top_lob;
   }

   OracleStatement statement(env);

   statement.append("INSERT /*+ NOLOGGING */ INTO %s VALUES(%d, EMPTY_BLOB())", _table_name.ptr(), no);
   statement.prepare();
   statement.execute();

   if (no > 0)
   {
      _Block &block = _blocks.push();

      block.size = 0;
   }

   _top_lob = _getLob(env, _blocks.size());
}

void BingoStorage::add (OracleEnv &env, const Array<char> &data, int &blockno, int &offset)
{
   if (_blocks.size() < 1 || _blocks.top().size + data.size() > _MAX_BLOCK_SIZE)
      _insertLOB(env, _blocks.size() + 1);

   _Block &top = _blocks.top();

   blockno = _blocks.size() - 1;
   offset = top.size;

   _top_lob->write(top.size, data.ptr(), data.size());

   _Addr addr;
   
   addr.blockno = _blocks.size() - 1;
   addr.length = data.size();
   addr.offset = top.size;
   top.size += data.size();
   
   _index_lob->write(_n_added * sizeof(_Addr), (char *)&addr, sizeof(_Addr));

   _n_added++;

   _State *state = _getState(true);
   
   if (state != 0)
      state->age++;
}

int BingoStorage::count ()
{
   return _index.size();
}

void BingoStorage::get (int n, Array<char> &out)
{
   const _Addr &addr = _index[n];
   
   const char *ptr = (const char *)_shmem_array[addr.blockno]->ptr();
   
   out.copy(ptr + addr.offset, addr.length);
}

BingoStorage::_State * BingoStorage::_getState (bool allow_first)
{
   return (_State *)_getShared(_shmem_state, _shmem_id.ptr(), sizeof(_State), allow_first);
}

void * BingoStorage::_getShared (SharedMemory * &sh_mem, char *name, int shared_size, bool allow_first)
{
   if (sh_mem != 0 && strcmp(sh_mem->getID(), name) != 0)
   {
      delete sh_mem;
      sh_mem = 0;
   }

   if (sh_mem == 0)
      sh_mem = new SharedMemory(name, shared_size, !allow_first);

   if (sh_mem->ptr() == 0)
   {
      delete sh_mem;
      sh_mem = 0;
      return 0;
   }
   
   return sh_mem->ptr();
}

void BingoStorage::flush (OracleEnv &env)
{
   // must delete LOB-s before commit
   if (_top_lob != 0)
   {
      env.dbgPrintf("flushing LOB\n");
      delete _top_lob;
   }
   if (_index_lob != 0)
   {
      env.dbgPrintf("flushing index LOB\n");
      delete _index_lob;
   }

   OracleStatement::executeSingle(env, "COMMIT");
   
   // get LOB-s back
   if (_top_lob != 0)
      _top_lob = _getLob(env, _blocks.size());
   if (_index_lob != 0)
      _index_lob = _getLob(env, 0);
}

void BingoStorage::finish (OracleEnv &env)
{
   if (_top_lob != 0)
   {
      delete _top_lob;
      _top_lob = 0;
   }
   if (_index_lob != 0)
   {
      delete _index_lob;
      _index_lob = 0;
   }
}

void BingoStorage::lock (OracleEnv &env)
{
   OracleStatement::executeSingle(env, "LOCK TABLE %s IN EXCLUSIVE MODE", _table_name.ptr());
}


void BingoStorage::markRemoved (OracleEnv &env, int blockno, int offset)
{
   OracleStatement statement(env);
   OracleLOB lob(env);

   statement.append("SELECT bindata FROM %s WHERE id = :id FOR UPDATE", _table_name.ptr());
   statement.prepare();
   statement.bindIntByName(":id", &blockno);
   statement.defineBlobByPos(1, lob);
   statement.execute();
   
   byte mark = 1;

   lob.write(offset, (char *)&mark, 1);

   _State *state = _getState(true);
   
   if (state != 0)
      state->age++;
}
