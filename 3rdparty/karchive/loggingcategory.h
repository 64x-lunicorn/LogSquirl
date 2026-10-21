/*
    Stands in for the loggingcategory.h that ECM's
    ecm_qt_declare_logging_category() writes when KArchive is built with its
    own CMake files (#834).

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KARCHIVE_LOGGINGCATEGORY_H
#define KARCHIVE_LOGGINGCATEGORY_H

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KArchiveLog)

#endif
