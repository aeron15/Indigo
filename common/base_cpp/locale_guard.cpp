/****************************************************************************
 * Copyright (C) 2011 EPAM Systems
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

#include "base_cpp/locale_guard.h"

using namespace indigo;

LocaleGuard::LocaleGuard ()
{
#if defined(__linux__) || defined(__APPLE__)
   _locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
   if (_locale != NULL)
      _baselocale = uselocale(_locale);
   else
      _baselocale = NULL;
#endif
}

LocaleGuard::~LocaleGuard ()
{
#if defined(__linux__) || defined(__APPLE__)
   if (_locale != NULL)
   {
      uselocale(_baselocale);
      freelocale(_locale);
   }
#endif
}
