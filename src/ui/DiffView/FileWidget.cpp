#include "FileWidget.h"
#include "DisclosureButton.h"
#include "EditButton.h"
#include "DiscardButton.h"
#include "LineStats.h"
#include "FileLabel.h"
#include "HunkWidget.h"
#include "Images.h"
#include "conf/Settings.h"
#include "ui/RepoView.h"
#include "ui/Badge.h"
#include "ui/FileContextMenu.h"
#include "git/Repository.h"

#include "git/Buffer.h"


#include <QCheckBox>
#include <QContextMenuEvent>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>

_FileWidget::Header::Header(
  const git::Diff &diff,
  const git::Patch &patch,
  bool binary,
  bool lfs,
  bool submodule,
  QWidget *parent)
  : QFrame(parent), mDiff(diff), mPatch(patch), mSubmodule(submodule)
{
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  QString name = patch.name();
  mCheck = new QCheckBox(this);
  mCheck->setVisible(diff.isStatusDiff());
  mCheck->setTristate(true);

  mStatusBadge = new Badge({}, this);

  git::Patch::LineStats lineStats;
  lineStats.additions = 0;
  lineStats.deletions = 0;
  mStats = new LineStats(lineStats, this);
  mStats->setVisible(false);

  mFileLabel = new FileLabel(name, submodule, this);

  QHBoxLayout *buttons = new QHBoxLayout;
  buttons->setContentsMargins(0,0,0,0);
  buttons->setSpacing(4);

  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(4,4,4,4);
  layout->addWidget(mCheck);
  layout->addSpacing(4);
  layout->addWidget(mStatusBadge);
  layout->addWidget(mStats);
  layout->addWidget(mFileLabel, 1);
  layout->addStretch();
  layout->addLayout(buttons);

  // Add LFS buttons.
  if (lfs) {
    Badge *lfsBadge = new Badge({Badge::Label(FileWidget::tr("LFS"), true)}, this);
    buttons->addWidget(lfsBadge);

    QToolButton *lfsLockButton = new QToolButton(this);
    bool locked = patch.repo().lfsIsLocked(patch.name());
    lfsLockButton->setText(locked ? FileWidget::tr("Unlock") : FileWidget::tr("Lock"));
    buttons->addWidget(lfsLockButton);

    connect(lfsLockButton, &QToolButton::clicked, [this, patch] {
      bool locked = patch.repo().lfsIsLocked(patch.name());
      RepoView::parentView(this)->lfsSetLocked({patch.name()}, !locked);
    });

    git::RepositoryNotifier *notifier = patch.repo().notifier();
    connect(notifier, &git::RepositoryNotifier::lfsLocksChanged, this,
    [this, patch, lfsLockButton] {
      bool locked = patch.repo().lfsIsLocked(patch.name());
      lfsLockButton->setText(locked ? FileWidget::tr("Unlock") : FileWidget::tr("Lock"));
    });

    mLfsButton = new QToolButton(this);
    mLfsButton->setText(FileWidget::tr("Show Object"));
    mLfsButton->setCheckable(true);

    buttons->addWidget(mLfsButton);
    buttons->addSpacing(8);
  }

  // Add edit button.
  mEdit = new EditButton(patch, -1, binary, lfs, this);
  mEdit->setToolTip(FileWidget::tr("Edit File"));
  buttons->addWidget(mEdit);

  // Add discard button.
  mDiscardButton = new DiscardButton(this);
  mDiscardButton->setVisible(false);
  mDiscardButton->setToolTip(FileWidget::tr("Discard File"));
  buttons->addWidget(mDiscardButton);
  connect(mDiscardButton, &QToolButton::clicked, this, &_FileWidget::Header::discard);

  mDisclosureButton = new DisclosureButton(this);
  mDisclosureButton->setToolTip(
    mDisclosureButton->isChecked() ? FileWidget::tr("Collapse File") : FileWidget::tr("Expand File"));
  connect(mDisclosureButton, &DisclosureButton::toggled, [this] {
    mDisclosureButton->setToolTip(
      mDisclosureButton->isChecked() ? FileWidget::tr("Collapse File") : FileWidget::tr("Expand File"));
  });
  buttons->addWidget(mDisclosureButton);

  updatePatch(patch);

  if (!diff.isStatusDiff())
    return;

  // Respond to check changes.
  connect(mCheck, &QCheckBox::clicked, [this](bool staged) {
      if (staged)
          emit stageStateChanged(Qt::Checked);
      else
          emit stageStateChanged(Qt::Unchecked);
  });

  // Set initial check state.
  updateCheckState();
}

void _FileWidget::Header::updatePatch(const git::Patch &patch) {
    char status = git::Diff::statusChar(patch.status());
    mStatusBadge->setLabels({Badge::Label(QChar(status))});

    git::Patch::LineStats lineStats = patch.lineStats();
    mStats->setStats(lineStats);
    mStats->setVisible(lineStats.additions > 0 || lineStats.deletions > 0);

    mFileLabel->setName(patch.name());
    if (patch.status() == GIT_DELTA_RENAMED)
      mFileLabel->setOldName(patch.name(git::Diff::OldFile));

    mEdit->updatePatch(patch, -1);

    mDiscardButton->setVisible(mDiff.isStatusDiff() && !mSubmodule && !patch.isConflicted());

}
QCheckBox *_FileWidget::Header::check() const
{
    return mCheck;
}

DisclosureButton *_FileWidget::Header::disclosureButton() const
{
    return mDisclosureButton;
}

QToolButton *_FileWidget::Header::lfsButton() const
{
    return mLfsButton;
}

void _FileWidget::Header::setStageState(git::Index::StagedState state) {
    if (state == git::Index::Staged)
        mCheck->setCheckState(Qt::Checked);
    else if (state == git::Index::Unstaged)
        mCheck->setCheckState(Qt::Unchecked);
    else
        mCheck->setCheckState(Qt::PartiallyChecked);
}

void _FileWidget::Header::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (mDisclosureButton->isEnabled())
    mDisclosureButton->toggle();
}

void _FileWidget::Header::contextMenuEvent(QContextMenuEvent *event)
{
  RepoView *view = RepoView::parentView(this);
  FileContextMenu menu(view, {mPatch.name()}, mDiff.index());
  menu.exec(event->globalPos());
}

void _FileWidget::Header::updateCheckState()
{
  bool disabled = false;
  Qt::CheckState state = Qt::Unchecked;
  switch (mDiff.index().isStaged(mPatch.name())) {
    case git::Index::Disabled:
      disabled = true;
      break;

    case git::Index::Unstaged:
      break;

    case git::Index::PartiallyStaged:
      state = Qt::PartiallyChecked;
      break;

    case git::Index::Staged:
      state = Qt::Checked;
      break;

    case git::Index::Conflicted:
      disabled = (mPatch.count() > 0);
      break;
  }

  mCheck->setCheckState(state);
  mCheck->setEnabled(!disabled);
}

//###############################################################################
//###############      FileWidget     ###########################################
//###############################################################################

FileWidget::FileWidget(
  DiffView *view,
  const git::Diff &diff,
  const git::Patch &patch,
  const git::Patch &staged,
  QWidget *parent)
  : QWidget(parent), mView(view), mDiff(diff), mPatch(patch)
{
  setObjectName("FileWidget");
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0,0,0,0);
  layout->setSpacing(0);

  git::Repository repo = RepoView::parentView(this)->repo();

  QString name = patch.name();
  QString path = repo.workdir().filePath(name);
  bool submodule = repo.lookupSubmodule(name).isValid();

  bool binary = patch.isBinary();
  if (patch.isUntracked()) {
    QFile dev(path);
    if (dev.open(QFile::ReadOnly)) {
      QByteArray content = dev.readAll();
      git::Buffer buffer(content.constData(), content.length());
      binary = buffer.isBinary();
    }
  }

  bool lfs = patch.isLfsPointer();
  mHeader = new _FileWidget::Header(diff, patch, binary, lfs, submodule, parent);
  connect(this, &FileWidget::stageStateChanged, mHeader, &_FileWidget::Header::setStageState);
  connect(mHeader, &_FileWidget::Header::stageStateChanged, this, &FileWidget::headerCheckStateChanged);
  connect(mHeader, &_FileWidget::Header::discard, this, &FileWidget::discard);
  layout->addWidget(mHeader);

  DisclosureButton *disclosureButton = mHeader->disclosureButton();
  connect(disclosureButton, &DisclosureButton::toggled, [this](bool visible) {

    if (mHeader->lfsButton() && !visible) {
      mHunks.first()->setVisible(false);
      if (!mImages.isEmpty())
        mImages.first()->setVisible(false);
      return;
    }

    if (mHeader->lfsButton() && visible) {
      bool checked = mHeader->lfsButton()->isChecked();
      mHunks.first()->setVisible(!checked);
      if (!mImages.isEmpty())
        mImages.first()->setVisible(checked);
      return;
    }

    foreach (HunkWidget *hunk, mHunks)
      hunk->setVisible(visible);
  });

  if (diff.isStatusDiff()) {
    // Collapse on check.
    connect(mHeader->check(), &QCheckBox::stateChanged, [this](int state) {
      mHeader->disclosureButton()->setChecked(state != Qt::Checked);
    });
  }

  // Try to load an image from the file.
  if (binary) {
    layout->addWidget(addImage(disclosureButton, mPatch));
    return;
  }


  mHunkLayout = new QVBoxLayout();
  layout->addLayout(mHunkLayout);

  updatePatch(patch, staged);

  // LFS
  if (QToolButton *lfsButton = mHeader->lfsButton()) {
    connect(lfsButton, &QToolButton::clicked,
    [this, layout, disclosureButton, lfsButton](bool checked) {
      lfsButton->setText(checked ? tr("Show Pointer") : tr("Show Object"));
      mHunks.first()->setVisible(!checked);

      // Image already loaded.
      if (!mImages.isEmpty()) {
        mImages.first()->setVisible(checked);
        return;
      }

      // Load image.
      layout->addWidget(addImage(disclosureButton, mPatch, true));
    });
  }

  // Start hidden when the file is checked.
  bool expand = (mHeader->check()->checkState() == Qt::Unchecked);

  if (Settings::instance()->value("collapse/added").toBool() == true &&
      patch.status() == GIT_DELTA_ADDED)
    expand = false;

  if (Settings::instance()->value("collapse/deleted").toBool() == true &&
      patch.status() == GIT_DELTA_DELETED)
    expand = false;

  disclosureButton->setChecked(expand);
}

void FileWidget::updateHunks(git::Patch stagedPatch)
{
    if (mSuppressUpdate)
        return;

    mSuppressUpdate = true;
    mSupressStaging = true;
    for (auto hunk: mHunks)
     hunk->load(stagedPatch, true);
    mSupressStaging = false;

    int staged = 0;
    int unstaged = 0;
    for (int i = 0; i < mHunks.size(); ++i) {
      git::Index::StagedState state = mHunks[i]->stageState();
      if (state == git::Index::Staged)
          staged++;
      else if (state == git::Index::Unstaged)
          unstaged ++;
    }

    mSuppressUpdate = false;

    if (staged == mHunks.size()) {
      emit stageStateChanged(git::Index::Staged);
      return;
    }

    if (unstaged == mHunks.size()) {
      emit stageStateChanged(git::Index::Unstaged);
      return;
    }

    emit stageStateChanged(git::Index::PartiallyStaged);
}

bool FileWidget::isEmpty()
{
  return (mHunks.isEmpty() && mImages.isEmpty());
}

void FileWidget::updatePatch(const git::Patch &patch, const git::Patch &staged) {
  mHeader->updatePatch(patch);

  git::Repository repo = RepoView::parentView(this)->repo();

  QString name = patch.name();
  QString path = repo.workdir().filePath(name);
  bool submodule = repo.lookupSubmodule(name).isValid();
  bool lfs = patch.isLfsPointer();

  // remove all hunks
  QLayoutItem *child;
  while ((child = mHunkLayout->takeAt(0)) != 0) {
        delete child;
  }
  // Add untracked file content.
  if (patch.isUntracked()) {
    if (!QFileInfo(path).isDir())
      mHunkLayout->addWidget(addHunk(mDiff, patch, staged, -1, lfs, submodule));
    return;
  }

  // Generate a diff between the head tree and index.
  QSet<int> stagedHunks;
  if (staged.isValid()) {
    for (int i = 0; i < staged.count(); ++i)
      stagedHunks.insert(staged.lineNumber(i, 0, git::Diff::OldFile));
  }

  // Add diff hunks.
  int hunkCount = patch.count();
  for (int hidx = 0; hidx < hunkCount; ++hidx) {
    HunkWidget *hunk = addHunk(mDiff, patch, staged, hidx, lfs, submodule);
    int startLine = patch.lineNumber(hidx, 0, git::Diff::OldFile);
    hunk->header()->check()->setChecked(stagedHunks.contains(startLine)); // not correct, because it could also only a part of the hunk staged (single lines)
    mHunkLayout->addWidget(hunk);
  }
}

_FileWidget::Header *FileWidget::header() const
{
    return mHeader;
}

const char* FileWidget::name() const { return mPatch.name(); }

QList<HunkWidget *> FileWidget::hunks() const { return mHunks; }

QWidget *FileWidget::addImage(
  DisclosureButton *button,
  const git::Patch patch,
  bool lfs)
{
  Images *images = new Images(patch, lfs, this);

  // Hide on file collapse.
  if (!lfs)
    connect(button, &DisclosureButton::toggled, images, &QLabel::setVisible);

  // Remember image.
  mImages.append(images);

  return images;
}

HunkWidget *FileWidget::addHunk(
  const git::Diff &diff,
  const git::Patch &patch,
  const git::Patch &staged,
  int index,
  bool lfs,
  bool submodule)
{
  HunkWidget *hunk =
    new HunkWidget(mView, diff, patch, staged, index, lfs, submodule, this);

  connect(hunk, &HunkWidget::stageStateChanged, [this]() {this->stageHunks(false);});
  connect(hunk, &HunkWidget::discardSignal, this, &FileWidget::discardHunk);
  TextEditor* editor = hunk->editor(false);

  // Respond to editor diagnostic signal.
  connect(editor, &TextEditor::diagnosticAdded,
  [this](int line, const TextEditor::Diagnostic &diag) {
    emit diagnosticAdded(diag.kind);
  });

  // Remember hunk.
  mHunks.append(hunk);

  return hunk;
}

void FileWidget::stageHunks(bool completeFile, bool completeFileStaged)
{
  if (mSupressStaging)
      return;

  git::Index index = mDiff.index();
  if (!index.isValid()) // why the index can be invalid?
      return;

  int staged = 0;
  int unstaged = 0;
  for (int i = 0; i < mHunks.size(); ++i) {
    git::Index::StagedState state = mHunks[i]->stageState();
    if (state == git::Index::Staged)
        staged++;
    else if (state == git::Index::Unstaged)
        unstaged ++;
  }

  mSuppressUpdate = true;

  if ((staged == mHunks.size() && mHunks.size() > 0) || (completeFile && completeFileStaged)) {
    // if the file does not contain hunks, it should be always staged!
    index.setStaged({mPatch.name()}, true);
    emit stageStateChanged(git::Index::Staged);
    mSuppressUpdate = false;
    return;
  } else if (completeFile && !completeFileStaged) {
      index.setStaged({mPatch.name()}, false);
      emit stageStateChanged(git::Index::Unstaged);
      mSuppressUpdate = false;
      return;
  }

  if (unstaged == mHunks.size()) {
    index.setStaged({mPatch.name()}, false);
    emit stageStateChanged(git::Index::Unstaged);
    mSuppressUpdate = false;
    return;
  }


  // when changing a line in a file,
  // two lines are visible, the old one and the new one.
  // When staging only one line, for example the added line,
  // then the unstaged (removed line) and the added line shall be
  // available in the file.
  QString name = mPatch.name();
  git::Repository repo = mPatch.repo();
  QFile dev(repo.workdir().filePath(name));
  if (!dev.open(QFile::ReadOnly))
      return;

  QByteArray fileContent = dev.readAll();
  dev.close();

  QByteArray buffer;
  QList<QList<QByteArray>> image;
  mPatch.populatePreimage(image, fileContent);
  for (int i = 0; i < mHunks.size(); ++i) {

    QByteArray hunk_content;
      hunk_content = mHunks[i]->apply();
      mPatch.apply(image, i, hunk_content);
  }
  buffer = mPatch.generateResult(image);

  // Add the buffer to the index.
  index.add(mPatch.name(), buffer);
  mSuppressUpdate = false;

  emit stageStateChanged(git::Index::PartiallyStaged);
}

void FileWidget::discardHunk() {
    HunkWidget* hunk = static_cast<HunkWidget*>(QObject::sender());
    git::Repository repo = mPatch.repo();
    if (mPatch.isUntracked()) {
      repo.workdir().remove(mPatch.name());
      return;
    }

    QString name = mPatch.name();
    QFile dev(repo.workdir().filePath(name));
    if (!dev.open(QFile::ReadOnly))
        return;

    QByteArray fileContent = dev.readAll();
    dev.close();

    QSaveFile file(repo.workdir().filePath(name));
    if (!file.open(QFile::WriteOnly))
      return;


    QByteArray buffer;
    for (int i = 0; i < mHunks.size(); ++i) {

      QByteArray hunk_content;
      if (mHunks[i] == hunk) {
        hunk_content = mHunks[i]->hunk();
        buffer = mPatch.apply(i, hunk_content, fileContent);
      }
    }

    file.write(buffer);
    if (!file.commit())
      return;

    // FIXME: Work dir changed?
    RepoView::parentView(this)->refresh();
}

void FileWidget::discard() {
    QString name = mPatch.name();
    bool untracked = mPatch.isUntracked();
    QString path = mPatch.repo().workdir().filePath(name);
    QString arg = QFileInfo(path).isDir() ? FileWidget::tr("Directory") : FileWidget::tr("File");
    QString title = untracked ? FileWidget::tr("Remove %1?").arg(arg) : FileWidget::tr("Discard Changes?");
    QString text = untracked ?
      FileWidget::tr("Are you sure you want to remove '%1'?") :
      FileWidget::tr("Are you sure you want to discard all changes in '%1'?");
    QMessageBox *dialog = new QMessageBox(
      QMessageBox::Warning, title, text.arg(name),
      QMessageBox::Cancel, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setInformativeText(FileWidget::tr("This action cannot be undone."));

    QString button =
      untracked ? FileWidget::tr("Remove %1").arg(arg) : FileWidget::tr("Discard Changes");
    QPushButton *discard =
      dialog->addButton(button, QMessageBox::AcceptRole);
    connect(discard, &QPushButton::clicked, [this, untracked] {
      RepoView *view = RepoView::parentView(this);
      git::Repository repo = mPatch.repo();
      QString name = mPatch.name();
      int strategy = GIT_CHECKOUT_FORCE;
      if (untracked) {
        QDir dir = repo.workdir();
        if (QFileInfo(dir.filePath(name)).isDir()) {
          if (dir.cd(name))
            dir.removeRecursively();
        } else {
          dir.remove(name);
        }
      } else if (!repo.checkout(git::Commit(), nullptr, {name}, strategy)) {
        LogEntry *parent = view->addLogEntry(mPatch.name(), FileWidget::tr("Discard"));
        view->error(parent, FileWidget::tr("discard"), mPatch.name());
      }

      // FIXME: Work dir changed?
      view->refresh();
    });

    dialog->open();
}

void FileWidget::headerCheckStateChanged(int state)
{
    assert(state != Qt::PartiallyChecked); // makes no sense, that the user can select partially selected

    mSupressStaging = true;
    bool staged = state == Qt::Checked ? true : false;
    for (int i = 0; i < mHunks.size(); ++i)
        mHunks[i]->setStaged(staged);
    mSupressStaging = false;

    // complete file must be staged
    stageHunks(true, staged);
}
