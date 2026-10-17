#pragma once

#include "WormProfile.h"
#include <QWidget>
#include <QStringList>
#include <atomic>
#include <memory>

class QAction;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSplitter;
class QTableWidget;
class ImageViewer;
struct WormMeasurementJobResult;
struct WormLabelPreviewResult;
template<typename T> class QFutureWatcher;

class WormProfileWorkspace : public QWidget
{
    Q_OBJECT
public:
    explicit WormProfileWorkspace(QWidget *parent = nullptr);
    ~WormProfileWorkspace() override;
    const WormProfile &profile() const { return m_profile; }
    QString profilePath() const { return m_path; }
    bool hasProfile() const { return m_active; }
    bool isDirty() const { return m_dirty; }
    bool isBusy() const { return m_watcher != nullptr; }
    bool maybeSave();
    void setExternalBusy(bool busy);
    bool createProfile(const QString &wormId);
    bool openProfileFile(const QString &path, QString *error);
    bool addImages(const QStringList &paths, const QDate &date);
    bool setClassMapping(const QMap<int, TissueClass> &mapping, QString *error = nullptr);
    bool exportCsvTo(const QString &path);
    QAction *newAction() const { return m_newAction; }
    QAction *openAction() const { return m_openAction; }
    QAction *saveAction() const { return m_saveAction; }
    QAction *exportAction() const { return m_exportAction; }

public slots:
    void newProfile();
    void openProfile();
    void saveProfile();
    void saveProfileAs();
    void importImages();
    void configureClasses();
    void reloadMeasurements();
    void exportCsv();
    void cancelJob();

signals:
    void stateChanged();
    void busyChanged(bool busy);
    void jobProgress(int value, int maximum);
    void statusMessage(const QString &message);

private:
    void refresh(const QString &selectedId = QString());
    void refreshSelection();
    void updateActions();
    void markDirty();
    void updateObservation();
    void removeObservation();
    void runMeasurements(const QString &exportPath = QString());
    void requestPreview();
    void startPreview();
    int selectedIndex() const;

    WormProfile m_profile;
    QString m_path;
    bool m_active = false;
    bool m_dirty = false;
    bool m_refreshing = false;
    bool m_externalBusy = false;
    int m_previewGeneration = 0;
    bool m_previewPending = false;
    QString m_previewId;
    QString m_previewHash;
    QMap<int, TissueClass> m_previewMapping;
    QFutureWatcher<WormMeasurementJobResult> *m_watcher = nullptr;
    QFutureWatcher<WormLabelPreviewResult> *m_previewWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QSplitter *m_splitter;
    QListWidget *m_observations;
    QLineEdit *m_wormId;
    QPlainTextEdit *m_profileNotes;
    QLabel *m_count;
    QLabel *m_heading;
    QLabel *m_metadata;
    QLabel *m_warning;
    QLabel *m_legend;
    ImageViewer *m_view;
    QTableWidget *m_table;
    QDateEdit *m_date;
    QDoubleSpinBox *m_pixelWidth;
    QDoubleSpinBox *m_pixelHeight;
    QLineEdit *m_observationNotes;
    std::array<QLabel *, 4> m_areas;
    QAction *m_newAction;
    QAction *m_openAction;
    QAction *m_saveAction;
    QAction *m_addAction;
    QAction *m_removeAction;
    QAction *m_classesAction;
    QAction *m_reloadAction;
    QAction *m_exportAction;
};
