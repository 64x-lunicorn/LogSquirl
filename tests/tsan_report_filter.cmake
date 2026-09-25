# The ctest runner leaves out only the ThreadSanitizer reports of races made
# inside a library that is not built with TSan, on both sides; every report
# with an access in LogSquirl's code, and anything else TSan prints, still
# fails the case (#482). The reports below are shortened from the CI job's.
#
# Usage: cmake -DMODULE_DIR=<repo>/cmake -DWORK_DIR=<scratch dir> -P tsan_report_filter.cmake

cmake_minimum_required(VERSION 3.16)

include("${MODULE_DIR}/TsanReportFilter.cmake")
set(_suppressions "${MODULE_DIR}/tsan.supp")

set(_failed "")

set(_tsan "(libtsan.so.2+0xa66c8) (BuildId: 2a13)")
set(_qt_new "    #0 operator new(unsigned long) ../../../../src/libsanitizer/tsan/tsan_new_delete.cpp:64 ${_tsan}")
set(_qt_delete "    #0 operator delete(void*, unsigned long) ../../../../src/libsanitizer/tsan/tsan_new_delete.cpp:150 ${_tsan}")

# Runs the filter over one log and checks what it decided.
function(expect name content failures left_out)
  set(_log "${WORK_DIR}/${name}/tsan.1")
  file(REMOVE_RECURSE "${WORK_DIR}/${name}")
  file(WRITE "${_log}" "${content}")
  logsquirl_tsan_filter(SUPPRESSIONS "${_suppressions}" LOGS "${_log}"
    OUTPUT_FILE "${_log}.sorted" FAILURES _failures LEFT_OUT _left_out)
  file(READ "${_log}.sorted" _output)
  if(NOT _failures EQUAL failures OR NOT _left_out EQUAL left_out)
    string(APPEND _failed "\n  ${name}: expected ${failures} failure(s) and ${left_out} left out, got ${_failures} and ${_left_out}\n${_output}")
  endif()
  foreach(_expected IN LISTS ARGN)
    string(FIND "${_output}" "${_expected}" _at)
    if(_at EQUAL -1)
      string(APPEND _failed "\n  ${name}: the output lacks '${_expected}':\n${_output}")
    endif()
  endforeach()
  set(_failed "${_failed}" PARENT_SCOPE)
endfunction()

# Qt frees on the main thread what it allocated on a pool thread, after
# handing it over through its event queue.
set(_inside_qtcore "==================
WARNING: ThreadSanitizer: data race (pid=162)
  Write of size 8 at 0x72100000c100 by main thread:
${_qt_delete}
    #1 QCoreApplicationPrivate::sendPostedEvents(QObject*, int, QThreadData*) <null> (libQt6Core.so.6+0x18d9fd) (BuildId: fcf7)
    #2 CATCH2_INTERNAL_TEST_2 /usr/local/tests/unit/lookuprunner_test.cpp:117 (logsquirl_tests+0x86219a) (BuildId: 0fc3)

  Previous write of size 8 at 0x72100000c100 by thread T1 (mutexes: write M0):
${_qt_new}
    #1 QFutureCallOutEvent::clone() const <null> (libQt6Core.so.6+0x360d87) (BuildId: fcf7)
    #2 QtConcurrent::RunFunctionTaskBase<std::optional<timelookup::Result> >::run() /opt/qt/6.11.2/gcc_64/include/QtConcurrent/qtconcurrentrunbase.h:84 (logsquirl_tests+0x8726c8)

SUMMARY: ThreadSanitizer: data race ../../../../src/libsanitizer/tsan/tsan_new_delete.cpp:150 in operator delete(void*, unsigned long)
==================
")
expect(inside_qtcore "${_inside_qtcore}ThreadSanitizer: reported 1 warnings\n" 0 1
  "left out 1 data race(s) between libQt6Core.so.6 and libQt6Core.so.6")

# A slot's argument array: Qt allocates it on the sending thread, the inlined
# Qt template that calls the slot reads it on the receiving one.
expect(argument_array "==================
WARNING: ThreadSanitizer: data race (pid=9)
  Read of size 8 at 0x723800000058 by thread T1:
    #0 QtPrivate::FunctorCall<std::integer_sequence<unsigned long, 0ul, 1ul>, QtPrivate::List<bool, int>, void, void (FileWatcherPollWorker::*)(bool, int)>::call() /opt/qt/6.11.2/gcc_64/include/QtCore/qobjectdefs_impl.h:116 (logsquirl_itests+0x7f1)
    #1 QtPrivate::QCallableObject<void (FileWatcherPollWorker::*)(bool, int), QtPrivate::List<bool, int>, void>::impl(int, QtPrivate::QSlotObjectBase*, QObject*, void**, bool*) /opt/qt/6.11.2/gcc_64/include/QtCore/qobjectdefs_impl.h:546 (logsquirl_itests+0x7f2)
    #2 QObject::event(QEvent*) <null> (libQt6Core.so.6+0x1e492c) (BuildId: fcf7)

  Previous write of size 8 at 0x723800000058 by main thread:
${_qt_new}
    #1 <null> <null> (libQt6Core.so.6+0x1e8caa) (BuildId: fcf7)
    #2 FileWatcher::applyWatchPolicy() /usr/local/src/filewatch/src/filewatcher.cpp:514 (logsquirl_itests+0x7f9cad)

SUMMARY: ThreadSanitizer: data race /opt/qt/6.11.2/gcc_64/include/QtCore/qobjectdefs_impl.h:116
==================
" 0 1)

# A QString payload shared through a queued signal: the worker drops its
# reference in an inlined decrement, QtCore drops the last one and frees it.
expect(reference_count "==================
WARNING: ThreadSanitizer: data race (pid=9)
  Write of size 8 at 0x722000009400 by main thread:
    #0 free ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:724 ${_tsan}
    #1 QVariant::~QVariant() <null> (libQt6Core.so.6+0x2059d7) (BuildId: fcf7)
    #2 main /usr/local/tests/ui/qtests_main.cpp:109 (logsquirl_itests+0x26fe21)

  Previous atomic write of size 4 at 0x722000009400 by thread T15 (mutexes: write M0):
    #0 std::__atomic_base<int>::fetch_sub(int, std::memory_order) /usr/include/c++/13/bits/atomic_base.h:645 (logsquirl_itests+0x28eab7)
    #1 bool QAtomicOps<int>::deref<int>(std::atomic<int>&) /opt/qt/6.11.2/gcc_64/include/QtCore/qatomic_cxx11.h:267 (logsquirl_itests+0x28eab7)
    #2 QArrayData::deref() /opt/qt/6.11.2/gcc_64/include/QtCore/qarraydata.h:79 (logsquirl_itests+0x28eab7)
    #3 QString::~QString() /opt/qt/6.11.2/gcc_64/include/QtCore/qstring.h:1462 (logsquirl_itests+0x224fa1)
    #4 IndexOperation::run() /usr/local/src/logdata/src/logdataworker.cpp:995 (logsquirl_itests+0x224fa1)

SUMMARY: ThreadSanitizer: data race in free
==================
" 0 1)

# glibc replaces the parsed TZ from another thread (mktime through Qt).
expect(inside_glibc "==================
WARNING: ThreadSanitizer: data race (pid=9)
  Write of size 8 at 0x720400001050 by thread T5 (mutexes: write M0, write M1):
    #0 free ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:724 ${_tsan}
    #1 <null> <null> (libc.so.6+0xe0880) (BuildId: a4a7)
    #2 <null> <null> (libQt6Core.so.6+0x107092) (BuildId: fcf7)
    #3 FullIndexOperation::doRun() /usr/local/src/logdata/src/logdataworker.cpp:1098 (logsquirl_itests+0x9d646f)

  Previous write of size 8 at 0x720400001050 by main thread:
    #0 malloc ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:665 ${_tsan}
    #1 strdup <null> (libc.so.6+0xb445e) (BuildId: a4a7)
    #2 qMkTime(tm*) <null> (libQt6Core.so.6+0x11246f) (BuildId: fcf7)

SUMMARY: ThreadSanitizer: data race in free
==================
" 0 1 "left out 1 data race(s) between libc.so.6 and libc.so.6")

# Both accesses in LogSquirl's code, one through an inlined std:: header, and
# a frame with characters a CMake list treats as syntax.
set(_in_logsquirl "==================
WARNING: ThreadSanitizer: data race (pid=4658)
  Read of size 8 at 0x721c00000a28 by thread T1:
    #0 std::__shared_ptr<QSemaphore, (__gnu_cxx::_Lock_policy)2>::get() const /usr/include/c++/13/bits/shared_ptr_base.h:1666 (logsquirl_tests+0xda2208)
    #1 operator() /usr/local/src/logdata/src/logfiltereddataworker.cpp:287 (logsquirl_tests+0xda2208)
    #2 run /usr/local/src/utils/include/runnable_lambda.h:39 (logsquirl_tests+0xda2208)
    #3 <null> <null> (libQt6Core.so.6+0x2a75ef) (BuildId: fcf7)

  Previous write of size 8 at 0x721c00000a28 by main thread (mutexes: write M0):
${_qt_new}
    #1 createRunnable<[with T = int; U = char]> /usr/local/src/utils/include/runnable_lambda.h:51 (logsquirl_tests+0xda292b)

SUMMARY: ThreadSanitizer: data race /usr/include/c++/13/bits/shared_ptr_base.h:1666 in get
==================
")
expect(in_logsquirl "${_in_logsquirl}" 1 0
  "logfiltereddataworker.cpp:287" "createRunnable<[with T = int; U = char]>")

# A slot of a queued signal reads the argument QtCore copied for the call on
# the emitting thread: Qt's event queue hands it over.
set(_queued_argument_read "  Read of size 8 at 0x720800006550 by main thread:
    #0 QString::size() const /opt/qt/6.11.2/gcc_64/include/QtCore/qstring.h:278 (logsquirl_openlogfile_tests+0x34403c)
    #1 SearchSession::handleSearchFinished(SearchId, LineNumber, bool, QString const&) /usr/local/src/logdata/src/searchsession.cpp:446 (logsquirl_openlogfile_tests+0x34403c)
    #2 QtPrivate::QCallableObject<void (SearchSession::*)(SearchId, LineNumber, bool, QString const&), QtPrivate::List<SearchId, LineNumber, bool, QString const&>, void>::impl(int, QtPrivate::QSlotObjectBase*, QObject*, void**, bool*) /opt/qt/6.11.2/gcc_64/include/QtCore/qobjectdefs_impl.h:546 (logsquirl_openlogfile_tests+0x1)
    #3 QObject::event(QEvent*) <null> (libQt6Core.so.6+0x1e492c) (BuildId: fcf7)
    #4 waitSearchSettled /usr/local/tests/openlogfile/openlogfile_test.cpp:147 (logsquirl_openlogfile_tests+0x1a0c19)
")
expect(queued_argument "==================
WARNING: ThreadSanitizer: data race (pid=7124)
${_queued_argument_read}
  Previous write of size 8 at 0x720800006550 by thread T13:
    #0 operator new(unsigned long, std::nothrow_t const&) ../../../../src/libsanitizer/tsan/tsan_new_delete.cpp:76 ${_tsan}
    #1 QMetaType::create(void const*) const <null> (libQt6Core.so.6+0x1b2341) (BuildId: fcf7)
    #2 SearchOperation::doSearch(SearchData&, LineNumber) /usr/local/src/logdata/src/logfiltereddataworker.cpp:610 (logsquirl_openlogfile_tests+0x332959)

SUMMARY: ThreadSanitizer: data race /opt/qt/6.11.2/gcc_64/include/QtCore/qstring.h:278 in QString::size() const
==================
" 0 1 "left out 1 data race(s) between a queued call's argument")

# The same read against a block QtCore allocated otherwise stays: nothing says
# the event queue handed it over.
expect(slot_reads_other_block "==================
WARNING: ThreadSanitizer: data race (pid=7124)
${_queued_argument_read}
  Previous write of size 8 at 0x720800006550 by thread T13:
${_qt_new}
    #1 QArrayData::reallocateUnaligned(QArrayData*, void*, long long, long long, QArrayData::AllocationOption) <null> (libQt6Core.so.6+0x1)
    #2 SearchOperation::doSearch(SearchData&, LineNumber) /usr/local/src/logdata/src/logfiltereddataworker.cpp:610 (logsquirl_openlogfile_tests+0x332959)

SUMMARY: ThreadSanitizer: data race
==================
" 1 0)

# And a queued argument read outside a slot Qt called for an event stays too.
expect(argument_read_elsewhere "==================
WARNING: ThreadSanitizer: data race (pid=7124)
  Read of size 8 at 0x720800006550 by thread T2:
    #0 SearchOperation::run() /usr/local/src/logdata/src/logfiltereddataworker.cpp:120 (logsquirl_tests+0x1)

  Previous write of size 8 at 0x720800006550 by thread T13:
    #0 operator new(unsigned long, std::nothrow_t const&) ../../../../src/libsanitizer/tsan/tsan_new_delete.cpp:76 ${_tsan}
    #1 QMetaType::create(void const*) const <null> (libQt6Core.so.6+0x1b2341) (BuildId: fcf7)

SUMMARY: ThreadSanitizer: data race
==================
" 1 0)

# Qt frees what LogSquirl's code still reads: LogSquirl's side keeps it.
expect(one_side_in_logsquirl "==================
WARNING: ThreadSanitizer: data race (pid=9)
  Read of size 4 at 0x7200 by thread T2:
    #0 SearchOperation::run() /usr/local/src/logdata/src/logfiltereddataworker.cpp:120 (logsquirl_tests+0x1)

  Previous write of size 8 at 0x7200 by main thread:
${_qt_delete}
    #1 QCoreApplicationPrivate::sendPostedEvents(QObject*, int, QThreadData*) <null> (libQt6Core.so.6+0x18d9fd)

SUMMARY: ThreadSanitizer: data race
==================
" 1 0)

# An atomic of LogSquirl's own is LogSquirl's, even against Qt's free.
expect(own_atomic "==================
WARNING: ThreadSanitizer: data race (pid=9)
  Atomic write of size 1 at 0x7200 by thread T2:
    #0 std::__atomic_base<bool>::store(bool, std::memory_order) /usr/include/c++/13/bits/atomic_base.h:481 (logsquirl_tests+0x1)
    #1 LookupRunner::cancel() /usr/local/src/ui/src/lookuprunner.cpp:80 (logsquirl_tests+0x2)

  Previous write of size 8 at 0x7200 by main thread:
${_qt_delete}
    #1 QObject::deleteLater() <null> (libQt6Core.so.6+0x18d9fd)

SUMMARY: ThreadSanitizer: data race
==================
" 1 0)

# A library that is not listed is not left out.
expect(unlisted_library "==================
WARNING: ThreadSanitizer: data race (pid=9)
  Write of size 8 at 0x7200 by main thread:
${_qt_delete}
    #1 <null> <null> (libQt6Widgets.so.6+0x1) (BuildId: 1)

  Previous write of size 8 at 0x7200 by thread T1:
${_qt_new}
    #1 <null> <null> (libQt6Widgets.so.6+0x2) (BuildId: 1)

SUMMARY: ThreadSanitizer: data race
==================
" 1 0)

# Any other report, and anything else TSan prints, fails the case.
expect(thread_leak "==================
WARNING: ThreadSanitizer: thread leak (pid=9)
  Thread T1 (tid=11, finished) created by main thread at:
    #0 pthread_create ../../../../src/libsanitizer/tsan/tsan_interceptors_posix.cpp:1022 ${_tsan}
    #1 QThread::start(QThread::Priority) <null> (libQt6Core.so.6+0x3575e4)

SUMMARY: ThreadSanitizer: thread leak
==================
" 1 0 "thread leak")
expect(tsan_error "ThreadSanitizer: CHECK failed: tsan_rtl.cpp:42 \"((0)) != (0)\"\n" 1 0 "CHECK failed")
expect(cut_off "==================
WARNING: ThreadSanitizer: data race (pid=9)
  Write of size 8 at 0x7200 by main thread:
" 1 0)

# Several reports and TSan's closing lines together: only the counts matter.
expect(mixed "${_inside_qtcore}${_inside_qtcore}${_in_logsquirl}ThreadSanitizer: reported 3 warnings
ThreadSanitizer: Matched 1 suppressions (pid=9):
1 race:something
" 1 2 "left out 2 data race(s)" "Matched 1 suppressions")

if(_failed)
  message(FATAL_ERROR "cmake/TsanReportFilter.cmake:${_failed}")
endif()
message("TsanReportFilter: every case sorted as expected")
