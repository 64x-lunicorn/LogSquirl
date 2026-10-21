/*
    Stands in for the karchive_export.h that ECM's ecm_generate_export_header()
    writes when KArchive is built with its own CMake files (#834). KArchive is a
    static library here, so nothing is exported, and the API deprecated in
    KF 6.13 (KArchiveDirectory::addEntry/removeEntry) is left out of the build.

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef KARCHIVE_EXPORT_H
#define KARCHIVE_EXPORT_H

#include <QtGlobal>

#define KARCHIVE_EXPORT
#define KARCHIVE_NO_EXPORT
#define KARCHIVE_DEPRECATED_EXPORT
#define KARCHIVE_DEPRECATED Q_DECL_DEPRECATED
#define KARCHIVE_DEPRECATED_VERSION(major, minor, text) Q_DECL_DEPRECATED_X(text)
#define KARCHIVE_DEPRECATED_VERSION_BELATED(major, minor, textmajor, textminor, text) Q_DECL_DEPRECATED_X(text)
#define KARCHIVE_ENABLE_DEPRECATED_SINCE(major, minor) 0
#define KARCHIVE_BUILD_DEPRECATED_SINCE(major, minor) 0

#endif
