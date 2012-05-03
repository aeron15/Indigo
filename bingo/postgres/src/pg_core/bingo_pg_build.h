/* 
 */

#ifndef _BINGO_PG_BUILD_H__
#define	_BINGO_PG_BUILD_H__

#include "base_cpp/auto_ptr.h"
#include "base_cpp/obj_array.h"
#include "base_cpp/exception.h"

#include "bingo_postgres.h"
#include "bingo_pg_index.h"
#include "bingo_pg_build_engine.h"

class BingoPgText;
class BingoPgConfig;

/*
 * Class for building and updating the bingo index
 */
class BingoPgBuild {
public:
   enum {
      MAX_CACHE_SIZE=100
   };
   BingoPgBuild(PG_OBJECT index, const char* schema_name,const char* index_schema, bool new_index);
   ~BingoPgBuild();

   /*
    * Inserts a new structure into the index
    * Returns true if insertion was successfull
    */
   bool insertStructure(PG_OBJECT item_ptr, BingoPgText& struct_text);
   void insertStructureParallel(PG_OBJECT item_ptr, uintptr_t text_ptr);
   void flush();

   DEF_ERROR("build engine");

private:
   BingoPgBuild(const BingoPgBuild&); //no implicit copy
   
//   static void _errorHandler(const char* message, void* context);

   void _prepareBuilding(const char* schema_name, const char* index_schema);
   void _prepareUpdating();

   /*
    * Index relation
    */
   PG_OBJECT _index;

   /*
    * Buffers section handler
    */
   BingoPgIndex _bufferIndex;

   indigo::AutoPtr<BingoPgBuildEngine> fp_engine;

   /*
    * There are two possible uses - build(true) and update(false)
    */
   bool _buildingState;

   indigo::ObjArray<BingoPgBuildEngine::StructCache> _parrallelCache;

//#ifdef BINGO_PG_INTEGRITY_DEBUG
//   indigo::AutoPtr<FileOutput> debug_fileoutput;
//#endif

};

#endif	/* BINGO_PG_BUILD_H */

