/*
    Stands in for the loggingcategory.cpp that ECM's
    ecm_qt_declare_logging_category() writes when KArchive is built with its
    own CMake files (#834): the category name and default severity upstream
    declares in src/CMakeLists.txt.

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "loggingcategory.h"

Q_LOGGING_CATEGORY(KArchiveLog, "kf.archive", QtWarningMsg)
