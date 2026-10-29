#include "ui/main_window.h"

#include <QPromise>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace insidious::ui {

class MainWindowAsyncTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void decodeCompletion_data();
    void decodeCompletion();
    void staleScanDoesNotReplaceSource_data();
    void staleScanDoesNotReplaceSource();
    void staleFolderLoadDoesNotReplaceBranch_data();
    void staleFolderLoadDoesNotReplaceBranch();
private:
    QTemporaryDir m_settingsDirectory;
};

void MainWindowAsyncTests::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("InsidiousTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AsyncResults"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void MainWindowAsyncTests::decodeCompletion_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("current-success") << 0;
    QTest::newRow("image-switch") << 1;
    QTest::newRow("source-switch") << 2;
    QTest::newRow("cancelled") << 3;
    QTest::newRow("stale-error") << 4;
}

void MainWindowAsyncTests::decodeCompletion()
{
    QFETCH(int, scenario);
    MainWindow window;
    core::ImageEntry entry;
    entry.absolutePath = QStringLiteral("/test/current.png");
    entry.displayName = QStringLiteral("current.png");
    window.m_controller.setCollection({ entry }, 0);
    core::DecodedImage initial;
    initial.image = QImage(4, 4, QImage::Format_RGB32);
    initial.image.fill(Qt::green);
    initial.sourceSize = initial.displaySize = initial.fullDisplaySize = QSize(4, 4);
    window.displayDecodedImage(initial);
    window.m_decodeGeneration = 10;
    window.m_imageRenderPending = true;
    MainWindow::DecodeJobResult job;
    job.generation = 10;
    job.entry = entry;
    job.cacheKey = QStringLiteral("completed-job");
    job.decoded = initial;
    job.decoded.image.fill(Qt::blue);
    QPromise<MainWindow::DecodeJobResult> promise;
    promise.start();
    QSignalSpy finished(&window.m_decodeWatcher, &QFutureWatcherBase::finished);
    window.m_decodeWatcher.setFuture(promise.future());
    // Complete explicitly after the state transition; no sleep/decode race is needed.
    if (scenario == 1 || scenario == 2 || scenario == 4) {
        ++window.m_decodeGeneration;
        entry.absolutePath = QStringLiteral("/test/new.png");
        window.m_controller.setCollection({ entry }, 0);
    }
    if (scenario == 2) {
        ++window.m_sourceGeneration;
        window.m_cache->clear();
    }
    if (scenario == 4) {
        job.decoded.errorMessage = QStringLiteral("old decode failure");
    }
    if (scenario == 3) {
        window.m_decodeWatcher.cancel();
    } else {
        promise.addResult(job);
    }
    promise.finish();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(window.m_currentDecoded.image.pixelColor(0, 0), scenario == 0 ? QColor(Qt::blue) : QColor(Qt::green));
    QCOMPARE(window.m_cache->contains(job.cacheKey), scenario == 0);
    QVERIFY(!window.m_imageRenderPending);
    QVERIFY(window.m_imageView->hasImage());
    QCOMPARE(window.m_controller.currentEntry()->absolutePath, entry.absolutePath);
}

void MainWindowAsyncTests::staleScanDoesNotReplaceSource_data()
{
    QTest::addColumn<bool>("stale");
    QTest::newRow("current") << false;
    QTest::newRow("stale") << true;
}

void MainWindowAsyncTests::staleScanDoesNotReplaceSource()
{
    QFETCH(bool, stale);
    MainWindow window;
    const auto currentRoot = std::make_shared<core::CollectionFolderNode>();
    window.m_sourceRoot = currentRoot;
    window.m_sourceRootPath = QStringLiteral("current-source");
    window.m_fileTreeModel->setRoot(currentRoot);
    window.m_sourceGeneration = 1;
    MainWindow::ScanJobResult job;
    job.generation = 1;
    job.result.root = std::make_shared<core::CollectionFolderNode>();
    job.result.rootPath = QStringLiteral("completed-source");
    job.result.root->loaded = true;
    QPromise<MainWindow::ScanJobResult> promise;
    promise.start();
    QSignalSpy finished(&window.m_scanWatcher, &QFutureWatcherBase::finished);
    window.m_scanWatcher.setFuture(promise.future());
    if (stale) {
        ++window.m_sourceGeneration;
    }
    promise.addResult(job);
    promise.finish();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(window.m_sourceRoot, stale ? currentRoot : job.result.root);
    QCOMPARE(window.m_sourceRootPath, stale ? QStringLiteral("current-source") : job.result.rootPath);
}

void MainWindowAsyncTests::staleFolderLoadDoesNotReplaceBranch_data()
{
    staleScanDoesNotReplaceSource_data();
}

void MainWindowAsyncTests::staleFolderLoadDoesNotReplaceBranch()
{
    QFETCH(bool, stale);
    MainWindow window;
    const auto root = std::make_shared<core::CollectionFolderNode>();
    window.m_sourceRoot = root;
    window.m_fileTreeModel->setRoot(root);
    window.m_loadingFolderPath = QStringLiteral("pending-folder");
    window.m_activeFolderPath = QStringLiteral("another-folder");
    window.m_sourceGeneration = 1;
    MainWindow::FolderLoadJobResult job;
    job.generation = 1;
    job.result.images.append(core::ImageEntry {});
    QPromise<MainWindow::FolderLoadJobResult> promise;
    promise.start();
    QSignalSpy finished(&window.m_folderLoadWatcher, &QFutureWatcherBase::finished);
    window.m_folderLoadWatcher.setFuture(promise.future());
    if (stale) {
        ++window.m_sourceGeneration;
    }
    promise.addResult(job);
    promise.finish();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(root->images.size(), stale ? 0 : 1);
    QCOMPARE(root->loaded, !stale);
    QCOMPARE(window.m_loadingFolderPath, QStringLiteral("pending-folder"));
}

} // namespace insidious::ui

QTEST_MAIN(insidious::ui::MainWindowAsyncTests)
#include "main_window_async_tests.moc"
