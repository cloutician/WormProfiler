#include "WormProfileWorkspace.h"
#include "DesktopIcons.h"
#include "ImageViewer.h"

#include <QAction>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPromise>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <exception>

struct WormMeasurementJobResult
{
    WormProfile profile;
    QString exportPath;
    QString error;
    bool canceled = false;
};

struct WormLabelPreviewResult
{
    QImage image;
    QString error;
    QString id;
    int generation = 0;
};

namespace {
QLabel *heading(QVBoxLayout *layout, const QString &text, bool panel = false)
{
    auto *label = new QLabel(text);
    label->setObjectName(panel ? "panelTitle" : "sectionTitle");
    layout->addWidget(label);
    return label;
}

QDateEdit *calendar(QWidget *parent, const QString &name)
{
    auto *edit = new QDateEdit(QDate::currentDate(), parent);
    edit->setObjectName(name);
    edit->setCalendarPopup(true);
    edit->setKeyboardTracking(false);
    edit->setDisplayFormat("dd MMM yyyy");
    edit->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    edit->calendarWidget()->setFirstDayOfWeek(Qt::Monday);
    edit->calendarWidget()->setGridVisible(false);
    edit->calendarWidget()->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    return edit;
}

QString pathIdentity(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    QString result = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}
}

WormProfileWorkspace::WormProfileWorkspace(QWidget *parent) : QWidget(parent)
{
    using S = DesktopIcons::Symbol;
    setObjectName("wormProfileWorkspace");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *toolbar = new QToolBar(this);
    toolbar->setObjectName("wormProfileToolbar");
    toolbar->setIconSize(QSize(18, 18));
    m_newAction = toolbar->addAction(DesktopIcons::icon(S::NewReview), "New profile...", this, &WormProfileWorkspace::newProfile);
    m_openAction = toolbar->addAction(DesktopIcons::icon(S::FolderOpen), "Open worm profile...", this, &WormProfileWorkspace::openProfile);
    m_saveAction = toolbar->addAction(DesktopIcons::icon(S::Save), "Save worm profile", this, &WormProfileWorkspace::saveProfile);
    m_newAction->setObjectName("actionNewWormProfile");
    m_openAction->setObjectName("actionOpenWormProfile");
    m_saveAction->setObjectName("actionSaveWormProfile");
    toolbar->addSeparator();
    m_addAction = toolbar->addAction(DesktopIcons::icon(S::Batch), "Add TIFFs...", this, &WormProfileWorkspace::importImages);
    m_addAction->setObjectName("actionAddProfileTiffs");
    m_removeAction = toolbar->addAction(DesktopIcons::icon(S::Close), "Remove observation", this, &WormProfileWorkspace::removeObservation);
    m_removeAction->setToolTip("Remove the selected observation from this profile; keep the TIFF file");
    m_classesAction = toolbar->addAction(DesktopIcons::icon(S::Layers), "Tissue classes...", this, &WormProfileWorkspace::configureClasses);
    m_classesAction->setObjectName("actionTissueClasses");
    m_reloadAction = toolbar->addAction(DesktopIcons::icon(S::Reset), "Reload measurements", this, &WormProfileWorkspace::reloadMeasurements);
    toolbar->addSeparator();
    auto *fit = toolbar->addAction(DesktopIcons::icon(S::Fit), "Fit segmentation");
    auto *actual = toolbar->addAction(DesktopIcons::icon(S::ActualSize), "Actual size");
    actual->setIconText("1:1");
    actual->setToolTip("One preview pixel per display pixel");
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    m_exportAction = toolbar->addAction(DesktopIcons::icon(S::Export), "Export CSV...", this, &WormProfileWorkspace::exportCsv);
    m_exportAction->setObjectName("actionExportWormCsv");
    for (auto *action : {m_newAction, m_addAction, m_classesAction, actual, m_exportAction}) {
        qobject_cast<QToolButton *>(toolbar->widgetForAction(action))->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    root->addWidget(toolbar);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName("wormProfileSplitter");
    m_splitter->setChildrenCollapsible(false);
    auto *browser = new QWidget(m_splitter);
    browser->setObjectName("wormProfileBrowser");
    browser->setMinimumWidth(190);
    auto *left = new QVBoxLayout(browser);
    left->setContentsMargins(10, 8, 10, 10);
    heading(left, "Worm profile", true);
    m_wormId = new QLineEdit(browser);
    m_wormId->setObjectName("wormId");
    m_wormId->setPlaceholderText("Worm ID");
    m_wormId->setAccessibleName("Worm ID");
    left->addWidget(m_wormId);
    m_profileNotes = new QPlainTextEdit(browser);
    m_profileNotes->setObjectName("wormProfileNotes");
    m_profileNotes->setPlaceholderText("Profile notes");
    m_profileNotes->setAccessibleName("Profile notes");
    m_profileNotes->setFixedHeight(74);
    left->addWidget(m_profileNotes);
    heading(left, "Observations");
    m_observations = new QListWidget(browser);
    m_observations->setObjectName("wormObservations");
    m_observations->setTextElideMode(Qt::ElideMiddle);
    m_observations->setIconSize(QSize(24, 24));
    left->addWidget(m_observations, 1);
    m_count = new QLabel(browser);
    m_count->setObjectName("secondaryText");
    m_count->setWordWrap(true);
    left->addWidget(m_count);

    auto *document = new QWidget(m_splitter);
    auto *center = new QVBoxLayout(document);
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(0);
    m_heading = new QLabel("Worm measurements", document);
    m_heading->setObjectName("imageHeader");
    m_heading->setTextFormat(Qt::PlainText);
    m_heading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    center->addWidget(m_heading);
    auto *views = new QTabWidget(document);
    views->setObjectName("wormProfileViews");
    views->setDocumentMode(true);
    m_view = new ImageViewer(views);
    m_view->setObjectName("wormSegmentationView");
    views->addTab(m_view, "Segmentation");
    m_table = new QTableWidget(views);
    m_table->setObjectName("wormMeasurementsTable");
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({"Date", "Image", "Donor (px)", "Host (px)", "Background (px)", "Eyes (px)", "Ignored (px)", "Status"});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    views->addTab(m_table, "Measurements");
    center->addWidget(views, 1);
    m_metadata = new QLabel(document);
    m_metadata->setObjectName("imageMetadata");
    m_metadata->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    center->addWidget(m_metadata);

    auto *scroll = new QScrollArea(m_splitter);
    scroll->setMinimumWidth(260);
    scroll->setWidgetResizable(true);
    auto *inspector = new QWidget(scroll);
    inspector->setObjectName("wormProfileInspector");
    auto *right = new QVBoxLayout(inspector);
    right->setContentsMargins(14, 8, 14, 12);
    right->setSpacing(8);
    heading(right, "Observation", true);
    m_date = calendar(inspector, "wormObservationDate");
    m_date->setAccessibleName("Observation date");
    right->addWidget(m_date);
    m_observationNotes = new QLineEdit(inspector);
    m_observationNotes->setObjectName("wormObservationNotes");
    m_observationNotes->setPlaceholderText("Observation notes");
    m_observationNotes->setAccessibleName("Observation notes");
    right->addWidget(m_observationNotes);
    m_warning = new QLabel(inspector);
    m_warning->setTextFormat(Qt::PlainText);
    m_warning->setWordWrap(true);
    m_warning->setTextFormat(Qt::PlainText);
    m_warning->setStyleSheet("color: #9b4a34;");
    right->addWidget(m_warning);
    heading(right, "Region areas");
    auto *regions = new QGridLayout();
    for (int index = 0; index < 4; ++index) {
        const auto tissue = static_cast<TissueClass>(index);
        auto *swatch = new QLabel(inspector);
        swatch->setFixedSize(10, 10);
        swatch->setStyleSheet("background: " + tissueClassColor(tissue).name() + "; border-radius: 2px;");
        regions->addWidget(swatch, index, 0);
        regions->addWidget(new QLabel(tissueClassName(tissue), inspector), index, 1);
        m_areas[index] = new QLabel(inspector);
        m_areas[index]->setObjectName("area" + tissueClassName(tissue));
        m_areas[index]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        regions->addWidget(m_areas[index], index, 2);
    }
    regions->setColumnStretch(2, 1);
    right->addLayout(regions);
    m_legend = new QLabel(inspector);
    m_legend->setObjectName("secondaryText");
    m_legend->setWordWrap(true);
    right->addWidget(m_legend);
    heading(right, "Pixel calibration");
    auto *scale = new QFormLayout();
    m_pixelWidth = new QDoubleSpinBox(inspector);
    m_pixelHeight = new QDoubleSpinBox(inspector);
    m_pixelWidth->setObjectName("wormPixelWidth");
    m_pixelHeight->setObjectName("wormPixelHeight");
    for (auto *spin : {m_pixelWidth, m_pixelHeight}) {
        spin->setDecimals(6);
        spin->setRange(0, 1e6);
        spin->setSingleStep(0.1);
        spin->setSpecialValueText("Unknown");
        spin->setMinimumWidth(110);
        spin->setKeyboardTracking(false);
    }
    scale->addRow("Width (um)", m_pixelWidth);
    scale->addRow("Height (um)", m_pixelHeight);
    right->addLayout(scale);
    right->addStretch();
    scroll->setWidget(inspector);
    m_splitter->setSizes({220, 850, 280});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    root->addWidget(m_splitter, 1);

    connect(fit, &QAction::triggered, m_view, &ImageViewer::fitImage);
    connect(actual, &QAction::triggered, m_view, &ImageViewer::actualSize);
    connect(m_observations, &QListWidget::currentRowChanged, this, &WormProfileWorkspace::refreshSelection);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (!m_refreshing && row >= 0) m_observations->setCurrentRow(row);
    });
    connect(m_wormId, &QLineEdit::textEdited, this, [this](const QString &text) {
        m_profile.wormId = text; markDirty(); updateActions();
    });
    connect(m_profileNotes, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_refreshing) { m_profile.notes = m_profileNotes->toPlainText(); markDirty(); }
    });
    connect(m_date, &QDateEdit::dateChanged, this, &WormProfileWorkspace::updateObservation);
    connect(m_observationNotes, &QLineEdit::textEdited, this, &WormProfileWorkspace::updateObservation);
    connect(m_pixelWidth, &QDoubleSpinBox::valueChanged, this, &WormProfileWorkspace::updateObservation);
    connect(m_pixelHeight, &QDoubleSpinBox::valueChanged, this, &WormProfileWorkspace::updateObservation);
    refresh();
}

WormProfileWorkspace::~WormProfileWorkspace()
{
    if (m_cancel) m_cancel->store(true);
    if (m_watcher) m_watcher->waitForFinished();
    if (m_previewWatcher) m_previewWatcher->waitForFinished();
}

int WormProfileWorkspace::selectedIndex() const { return m_observations->currentRow(); }

void WormProfileWorkspace::markDirty()
{
    m_dirty = true;
    emit stateChanged();
}

void WormProfileWorkspace::setExternalBusy(bool busy)
{
    m_externalBusy = busy;
    updateActions();
}

void WormProfileWorkspace::updateActions()
{
    const bool idle = !isBusy() && !m_externalBusy;
    m_newAction->setEnabled(idle);
    m_openAction->setEnabled(idle);
    m_saveAction->setEnabled(m_active && idle);
    m_addAction->setEnabled(m_active && idle);
    m_removeAction->setEnabled(selectedIndex() >= 0 && idle);
    m_classesAction->setEnabled(m_active && !m_profile.observations.isEmpty() && idle);
    m_reloadAction->setEnabled(m_active && !m_profile.observations.isEmpty() && idle);
    const QString problem = m_profile.exportProblem();
    m_exportAction->setEnabled(m_active && idle && problem.isEmpty());
    m_exportAction->setToolTip(problem.isEmpty() ? "Export dated tissue measurements to CSV" : problem);
    m_splitter->setEnabled(!isBusy());
    m_wormId->setEnabled(m_active);
    m_profileNotes->setEnabled(m_active);
}

void WormProfileWorkspace::refresh(const QString &selectedId)
{
    m_refreshing = true;
    const int oldIndex = selectedIndex();
    const QString id = !selectedId.isEmpty() ? selectedId : oldIndex >= 0 && oldIndex < m_profile.observations.size()
        ? m_profile.observations[oldIndex].id : QString();
    m_profile.sortObservations();
    m_wormId->setText(m_profile.wormId);
    m_profileNotes->setPlainText(m_profile.notes);
    const QSignalBlocker blocker(m_observations);
    m_observations->clear();
    m_table->setRowCount(m_profile.observations.size());
    int rowToSelect = m_profile.observations.isEmpty() ? -1 : 0;
    int ready = 0;
    for (int row = 0; row < m_profile.observations.size(); ++row) {
        const auto &observation = m_profile.observations[row];
        if (observation.id == id) rowToSelect = row;
        const auto areas = m_profile.areas(observation);
        const bool valid = observation.statistics.valid();
        const bool mapped = valid && !areas.unmapped;
        if (mapped) ++ready;
        auto *item = new QListWidgetItem(DesktopIcons::icon(valid ? DesktopIcons::Symbol::Image : DesktopIcons::Symbol::Warning),
            observation.date.toString("dd MMM yyyy") + "\n" + QFileInfo(observation.imagePath).fileName(), m_observations);
        item->setToolTip(observation.imagePath);
        item->setSizeHint(QSize(180, 54));
        const QString state = !valid ? (observation.statistics.error.isEmpty() ? "Not measured" : observation.statistics.error)
            : areas.unmapped ? "Unmapped labels" : (observation.pixelWidthUm > 0) != (observation.pixelHeightUm > 0)
            ? "Incomplete calibration" : "Ready";
        const QStringList texts{observation.date.toString(Qt::ISODate), QFileInfo(observation.imagePath).fileName(),
            mapped ? QString::number(areas.pixels[0]) : "-", mapped ? QString::number(areas.pixels[1]) : "-",
            mapped ? QString::number(areas.pixels[2]) : "-", mapped ? QString::number(areas.pixels[3]) : "-",
            mapped ? QString::number(areas.pixels[4]) : "-", state};
        for (int column = 0; column < texts.size(); ++column) {
            auto *cell = new QTableWidgetItem(texts[column]);
            cell->setToolTip(column == 1 ? observation.imagePath : state);
            if (column >= 2 && column <= 6) cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, column, cell);
        }
    }
    m_count->setText(QString("%1 observations\n%2 measured and mapped").arg(m_profile.observations.size()).arg(ready));
    m_observations->setCurrentRow(rowToSelect);
    m_refreshing = false;
    refreshSelection();
    updateActions();
    emit stateChanged();
}

void WormProfileWorkspace::refreshSelection()
{
    if (m_refreshing) return;
    m_refreshing = true;
    const int index = selectedIndex();
    const bool selected = index >= 0 && index < m_profile.observations.size();
    for (QWidget *widget : std::array<QWidget *, 4>{m_date, m_observationNotes, m_pixelWidth, m_pixelHeight}) widget->setEnabled(selected);
    m_warning->clear();
    m_legend->clear();
    for (auto *label : m_areas) label->setText("-");
    if (!selected) {
        m_heading->setText(m_active ? m_profile.wormId : "Worm measurements");
        m_metadata->setText("No observation selected");
        m_observationNotes->clear();
        m_pixelWidth->setValue(0); m_pixelHeight->setValue(0);
    } else {
        const auto &observation = m_profile.observations[index];
        m_date->setDate(observation.date);
        m_observationNotes->setText(observation.notes);
        m_pixelWidth->setValue(observation.pixelWidthUm);
        m_pixelHeight->setValue(observation.pixelHeightUm);
        m_heading->setText(QFileInfo(observation.imagePath).fileName());
        m_heading->setToolTip(observation.imagePath);
        const auto &statistics = observation.statistics;
        m_metadata->setText(statistics.valid() ? QString("%1 x %2 px  |  %3-bit labels").arg(statistics.size.width()).arg(statistics.size.height()).arg(statistics.bitDepth)
                                             : "Not measured");
        if (!statistics.valid()) m_warning->setText(statistics.error.isEmpty() ? "Measurements not loaded." : statistics.error);
        else {
            const TissueAreas areas = m_profile.areas(observation);
            const bool calibrated = observation.pixelWidthUm > 0 && observation.pixelHeightUm > 0;
            for (int tissue = 0; tissue < 4; ++tissue) {
                QStringList values;
                for (auto it = m_profile.classMapping.cbegin(); it != m_profile.classMapping.cend(); ++it)
                    if (static_cast<int>(it.value()) == tissue) values << QString::number(it.key());
                m_areas[tissue]->setToolTip("TIFF labels: " + values.join(", "));
                if (!areas.unmapped) m_areas[tissue]->setText(QString("%L1 px").arg(areas.pixels[tissue])
                    + (calibrated ? QString("\n%1 um2").arg(areas.pixels[tissue] * observation.pixelWidthUm * observation.pixelHeightUm, 0, 'g', 8) : QString()));
            }
            m_legend->setText(areas.unmapped ? "Class mapping incomplete"
                : QString("Worm: %L1 px\nIgnored: %L2 px").arg(areas.wormPixels()).arg(areas.pixels[4]));
            if (areas.unmapped) m_warning->setText(QString("%L1 unmapped pixels. Assign tissue classes before exporting.").arg(areas.unmapped));
            else if ((observation.pixelWidthUm > 0) != (observation.pixelHeightUm > 0)) m_warning->setText("Set both pixel dimensions, or leave both Unknown.");
        }
        m_table->selectRow(index);
    }
    m_refreshing = false;
    requestPreview();
    updateActions();
}

void WormProfileWorkspace::updateObservation()
{
    if (m_refreshing || isBusy()) return;
    const int index = selectedIndex();
    if (index < 0 || index >= m_profile.observations.size()) return;
    auto &observation = m_profile.observations[index];
    const QString id = observation.id;
    observation.date = m_date->date();
    observation.notes = m_observationNotes->text();
    observation.pixelWidthUm = m_pixelWidth->value();
    observation.pixelHeightUm = m_pixelHeight->value();
    markDirty();
    refresh(id);
}

bool WormProfileWorkspace::createProfile(const QString &wormId)
{
    if (isBusy() || m_externalBusy || wormId.trimmed().isEmpty()) return false;
    m_profile = {};
    m_profile.wormId = wormId.trimmed();
    m_path.clear(); m_active = true; m_dirty = true;
    refresh();
    return true;
}

void WormProfileWorkspace::newProfile()
{
    if (!maybeSave()) return;
    bool accepted = false;
    const QString id = QInputDialog::getText(this, "New Worm Profile", "Worm ID", QLineEdit::Normal, QString(), &accepted);
    if (accepted) createProfile(id);
}

bool WormProfileWorkspace::openProfileFile(const QString &path, QString *error)
{
    if (isBusy() || m_externalBusy) return false;
    WormProfile loaded;
    if (!WormProfile::load(path, &loaded, error)) return false;
    m_profile = std::move(loaded);
    m_path = path; m_active = true; m_dirty = false;
    refresh();
    if (!m_profile.observations.isEmpty()) runMeasurements();
    return true;
}

void WormProfileWorkspace::openProfile()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, "Open Worm Profile", m_path, "Worm Profiles (*.wormprofile.json);;JSON (*.json)");
    if (path.isEmpty()) return;
    QString error;
    if (!openProfileFile(path, &error)) QMessageBox::warning(this, "Profile Not Opened", error);
}

void WormProfileWorkspace::saveProfile()
{
    if (m_path.isEmpty()) { saveProfileAs(); return; }
    QString error;
    if (!m_profile.save(m_path, &error)) { QMessageBox::warning(this, "Profile Not Saved", error); return; }
    m_dirty = false;
    emit stateChanged();
    emit statusMessage("Worm profile saved: " + m_path);
}

void WormProfileWorkspace::saveProfileAs()
{
    if (!m_active || isBusy()) return;
    QString path = QFileDialog::getSaveFileName(this, "Save Worm Profile", m_path.isEmpty() ? "worm-profile.wormprofile.json" : m_path, "Worm Profiles (*.wormprofile.json)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".wormprofile.json", Qt::CaseInsensitive)) path += ".wormprofile.json";
    QString error;
    if (!m_profile.save(path, &error)) { QMessageBox::warning(this, "Profile Not Saved", error); return; }
    m_path = path; m_dirty = false;
    emit stateChanged();
    emit statusMessage("Worm profile saved: " + path);
}

bool WormProfileWorkspace::maybeSave()
{
    if (!m_dirty) return true;
    const auto answer = QMessageBox::warning(this, "Unsaved Worm Profile", "Save changes to this worm profile?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Discard) return true;
    saveProfile();
    return !m_dirty;
}

bool WormProfileWorkspace::addImages(const QStringList &paths, const QDate &date)
{
    if (!m_active || isBusy() || m_externalBusy || !date.isValid()) return false;
    bool added = false;
    QSet<QString> existing;
    for (const auto &observation : m_profile.observations) existing.insert(pathIdentity(observation.imagePath));
    for (const auto &path : paths) {
        const QString identity = pathIdentity(path);
        if (existing.contains(identity)) continue;
        WormObservation observation;
        observation.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        observation.imagePath = QFileInfo(path).absoluteFilePath();
        observation.date = date;
        m_profile.observations.append(observation);
        existing.insert(identity); added = true;
    }
    if (!added) { emit statusMessage("No new TIFFs added; duplicate files were skipped."); return false; }
    markDirty();
    refresh();
    runMeasurements();
    return true;
}

void WormProfileWorkspace::importImages()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Add ilastik Segmentations", QString(), "TIFF Segmentation (*.tif *.tiff)");
    if (paths.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName("wormImportDateDialog");
    dialog.setWindowTitle("Observation Date");
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *date = calendar(&dialog, "wormImportDate");
    form->addRow("Date", date);
    form->addRow("Selected TIFFs", new QLabel(QString::number(paths.size()), &dialog));
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Add observations");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) addImages(paths, date->date());
}

void WormProfileWorkspace::removeObservation()
{
    const int index = selectedIndex();
    if (index < 0 || isBusy()) return;
    m_profile.observations.removeAt(index);
    markDirty(); refresh();
}

bool WormProfileWorkspace::setClassMapping(const QMap<int, TissueClass> &mapping, QString *error)
{
    if (isBusy() || m_externalBusy) return false;
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
        if (it.key() < 0 || it.key() > 65535 || static_cast<int>(it.value()) < 0 || static_cast<int>(it.value()) > 4) {
            if (error) *error = "Invalid label or tissue class.";
            return false;
        }
    }
    m_profile.classMapping = mapping;
    markDirty(); refresh();
    return true;
}

void WormProfileWorkspace::configureClasses()
{
    QMap<int, quint64> labels;
    for (const auto &observation : m_profile.observations) {
        if (observation.statistics.valid()) for (auto it = observation.statistics.labelPixels.cbegin(); it != observation.statistics.labelPixels.cend(); ++it) labels[it.key()] += it.value();
    }
    for (auto it = m_profile.classMapping.cbegin(); it != m_profile.classMapping.cend(); ++it) if (!labels.contains(it.key())) labels.insert(it.key(), 0);
    if (labels.isEmpty()) { emit statusMessage("No valid segmentation labels are available."); return; }
    QDialog dialog(this);
    dialog.setObjectName("wormTissueClassesDialog");
    dialog.setWindowTitle("Tissue Classes");
    dialog.resize(520, 390);
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(labels.size(), 3, &dialog);
    table->setHorizontalHeaderLabels({"TIFF label", "Pixels", "Tissue"});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QMap<int, QComboBox *> choices;
    int row = 0;
    for (auto it = labels.cbegin(); it != labels.cend(); ++it, ++row) {
        table->setItem(row, 0, new QTableWidgetItem(QString::number(it.key())));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
        auto *choice = new QComboBox(table);
        choice->setObjectName(QString("classForLabel_%1").arg(it.key()));
        choice->setMinimumWidth(220);
        choice->addItem("Unassigned", -1);
        for (int tissue = 0; tissue < 5; ++tissue) {
            QPixmap swatch(12, 12); swatch.fill(tissueClassColor(static_cast<TissueClass>(tissue)));
            choice->addItem(QIcon(swatch), tissueClassName(static_cast<TissueClass>(tissue)), tissue);
        }
        if (m_profile.classMapping.contains(it.key())) choice->setCurrentIndex(static_cast<int>(m_profile.classMapping.value(it.key())) + 1);
        choices.insert(it.key(), choice);
        table->setCellWidget(row, 2, choice);
        table->setRowHeight(row, 40);
    }
    layout->addWidget(table);
    auto *error = new QLabel(&dialog);
    error->setStyleSheet("color: #9b4a34;");
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Use mapping");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QMap<int, TissueClass> mapping;
        for (auto it = choices.cbegin(); it != choices.cend(); ++it) {
            const int tissue = it.value()->currentData().toInt();
            if (tissue < 0) { error->setText(QString("Assign a tissue or Ignore for label %1.").arg(it.key())); return; }
            mapping.insert(it.key(), static_cast<TissueClass>(tissue));
        }
        if (setClassMapping(mapping)) dialog.accept();
    });
    dialog.exec();
}

void WormProfileWorkspace::reloadMeasurements()
{
    if (!m_profile.observations.isEmpty() && !isBusy() && !m_externalBusy) runMeasurements();
}

void WormProfileWorkspace::runMeasurements(const QString &exportPath)
{
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_cancel;
    const WormProfile snapshot = m_profile;
    m_watcher = new QFutureWatcher<WormMeasurementJobResult>(this);
    auto *watcher = m_watcher;
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this, [this, watcher](int value) { emit jobProgress(value, watcher->progressMaximum()); });
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto result = watcher->result();
        m_watcher = nullptr;
        watcher->deleteLater();
        m_profile = result.profile;
        refresh();
        emit busyChanged(false);
        emit stateChanged();
        if (result.canceled) emit statusMessage("Measurement job canceled.");
        else if (!result.error.isEmpty()) {
            emit statusMessage(result.error);
            QMessageBox::warning(this, "Measurement Job Failed", result.error);
        } else if (!result.exportPath.isEmpty()) emit statusMessage("Measurements exported: " + result.exportPath);
        else {
            int failed = 0;
            for (const auto &observation : m_profile.observations) if (!observation.statistics.valid()) ++failed;
            emit statusMessage(QString("Measured %1 observations; %2 unreadable or unsupported.").arg(m_profile.observations.size() - failed).arg(failed));
        }
    });
    emit busyChanged(true);
    updateActions();
    emit statusMessage(exportPath.isEmpty() ? "Measuring segmentation TIFFs..." : "Validating TIFFs and exporting measurements...");
    watcher->setFuture(QtConcurrent::run([snapshot, cancel, exportPath](QPromise<WormMeasurementJobResult> &promise) {
        WormMeasurementJobResult result;
        result.profile = snapshot;
        result.exportPath = exportPath;
        promise.setProgressRange(0, snapshot.observations.size());
        try {
            result.canceled = !WormMeasurements::measureProfile(&result.profile, [&promise, cancel](int value, int) {
                promise.setProgressValue(value); return !cancel->load();
            });
            if (!result.canceled && !exportPath.isEmpty()) result.profile.writeCsv(exportPath, &result.error);
        } catch (const std::exception &exception) { result.error = QString::fromUtf8(exception.what()); }
        promise.addResult(result);
    }));
}

void WormProfileWorkspace::cancelJob() { if (m_cancel) m_cancel->store(true); }

bool WormProfileWorkspace::exportCsvTo(const QString &path)
{
    if (isBusy() || m_externalBusy || !m_profile.exportProblem().isEmpty() || path.isEmpty()) return false;
    runMeasurements(path);
    return true;
}

void WormProfileWorkspace::exportCsv()
{
    QString path = QFileDialog::getSaveFileName(this, "Export Worm Measurements", "worm-measurements.csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    exportCsvTo(path);
}

void WormProfileWorkspace::requestPreview()
{
    const int index = selectedIndex();
    if (index >= 0 && index < m_profile.observations.size() && m_profile.observations[index].statistics.valid()) {
        const auto &observation = m_profile.observations[index];
        if (m_previewId == observation.id && m_previewHash == observation.statistics.sha256
            && m_previewMapping == m_profile.classMapping && (m_view->hasImage() || m_previewWatcher)) return;
        m_previewId = observation.id;
        m_previewHash = observation.statistics.sha256;
        m_previewMapping = m_profile.classMapping;
    } else {
        m_previewId.clear(); m_previewHash.clear(); m_previewMapping.clear();
    }
    ++m_previewGeneration;
    m_view->setImage({});
    if (m_previewWatcher) m_previewPending = true;
    else startPreview();
}

void WormProfileWorkspace::startPreview()
{
    const int index = selectedIndex();
    if (index < 0 || index >= m_profile.observations.size() || !m_profile.observations[index].statistics.valid()) return;
    const auto observation = m_profile.observations[index];
    const auto mapping = m_profile.classMapping;
    const int generation = m_previewGeneration;
    m_previewWatcher = new QFutureWatcher<WormLabelPreviewResult>(this);
    auto *watcher = m_previewWatcher;
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto result = watcher->result();
        m_previewWatcher = nullptr;
        watcher->deleteLater();
        if (result.generation == m_previewGeneration) {
            m_view->setImage(result.image);
            if (!result.error.isEmpty()) {
                m_warning->setText(result.error);
                const int current = selectedIndex();
                if (current >= 0 && m_profile.observations[current].id == result.id) m_profile.observations[current].statistics.error = result.error;
                updateActions();
            }
            emit stateChanged();
        }
        if (m_previewPending) { m_previewPending = false; startPreview(); }
    });
    watcher->setFuture(QtConcurrent::run([observation, mapping, generation] {
        WormLabelPreviewResult result;
        result.id = observation.id; result.generation = generation;
        try { result.image = WormMeasurements::previewTiff(observation.imagePath, mapping, observation.statistics.sha256, &result.error); }
        catch (const std::exception &exception) { result.error = QString::fromUtf8(exception.what()); }
        return result;
    }));
}
