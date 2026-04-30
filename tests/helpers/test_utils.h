#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <string>
#include <chrono>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
/*
struct TestTimer {
    TestTimer()
        : TestTimer(
                ::testing::UnitTest::GetInstance()->current_test_info()->test_case_name() ) {
    text_ += std::string {"."} + std::string {::testing::UnitTest::GetInstance()->current_test_info()->name() };
    }

    TestTimer(const std::string& text)
        : Start { std::chrono::system_clock::now() }
        , text_ {text} {}

    virtual ~TestTimer() {
        using namespace std;
        Stop = chrono::system_clock::now();
        Elapsed = chrono::duration_cast<chrono::microseconds>(Stop - Start);
        cout << endl << text_ << " elapsed time = "
            << Elapsed.count() * 0.001 << "ms" << endl;
    }

    std::chrono::time_point<std::chrono::system_clock> Start;
    std::chrono::time_point<std::chrono::system_clock> Stop;
    std::chrono::microseconds Elapsed;
    std::string text_;
};
*/
class SafeQSignalSpy : public QSignalSpy {
  public:
    template <typename... Args>
    SafeQSignalSpy( Args&&... agruments )
        : QSignalSpy( std::forward<Args>(agruments)... ) {}

    bool safeWait( int timeout = 10000 ) {
        // If it has already been received
        bool result = count() > 0;
        if ( ! result ) {
            result = wait( timeout );
        }
        return result;
    }
};

// Poll until checkFunc() returns true, processing Qt events between checks.
// Uses a bounded QEventLoop instead of QTest::qWait() to ensure QTimer
// events (e.g. KDSignalThrottler) are reliably dispatched on all platforms.
// QTest::qWait() polls with processEvents() which can miss timer events
// on Windows CI when the timer fires at the polling boundary (#50).
template<typename F>
bool waitUiState( F&& checkFunc )
{
    QElapsedTimer elapsed;
    elapsed.start();

    while ( elapsed.elapsed() < 20000 ) {
        if ( checkFunc() ) {
            return true;
        }
        // Run a proper event loop for 100 ms so that QTimer events are
        // dispatched reliably.  The singleShot guard prevents hangs.
        QEventLoop loop;
        QTimer::singleShot( 100, &loop, &QEventLoop::quit );
        loop.exec();
    }
    return false;
};

#endif
