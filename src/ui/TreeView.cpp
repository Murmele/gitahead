//
//          Copyright (c) 2020
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Martin Marmsoler
//

#include "TreeView.h"
#include "ColumnView.h"
#include "ViewDelegate.h"
#include "TreeModel.h"
#include <QFormLayout>
#include <QItemDelegate>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include "TreeProxy.h"
#include <QMenu>
#include "DiffTreeModel.h"
#include "RepoView.h"
#include <QMessageBox>
#include <QPushButton>

#ifdef Q_OS_WIN
#define ICON_SIZE 48
#define SCROLL_BAR_WIDTH 18
#else
#define ICON_SIZE 64
#define SCROLL_BAR_WIDTH 0
#endif

namespace {

const QString kNameFmt = "<p style='font-size: large'>%1</p>";
const QString kLabelFmt = "<p style='color: gray; font-weight: bold'>%1</p>";

} // anon. namespace

TreeView::TreeView(QWidget *parent)
  : QTreeView(parent), mSharedDelegate(new ViewDelegate(this))
{
}

void TreeView::setModel(QAbstractItemModel *model)
{
  QTreeView::setModel(model);
  connect(selectionModel(), &QItemSelectionModel::selectionChanged,
		  this, &TreeView::handleSelectionChange);
  connect(model, &QAbstractItemModel::dataChanged, this, QOverload<const QModelIndex &, const QModelIndex&, const QVector<int>&>::of(&TreeView::updateCollapseCount));
  connect(this, &QTreeView::collapsed, this, &TreeView::itemCollapsed);
  connect(this, &QTreeView::expanded, this, &TreeView::itemExpanded);
  connect(model, &QAbstractItemModel::rowsInserted, this, QOverload<const QModelIndex &, int, int>::of(&TreeView::updateCollapseCount));

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &TreeView::customContextMenuRequested, this, &TreeView::onCustomContextMenu);
}

void TreeView::discard(const QModelIndex& index)
{
    auto p = qobject_cast<QSortFilterProxyModel*>(this->model());
    assert(p);
    auto m = qobject_cast<DiffTreeModel*>(p->sourceModel());
    assert(m);
    auto sIndex = p->mapToSource(index);
    int patchIndex = sIndex.data(DiffTreeModel::Role::PatchIndexRole).toInt();
    QString name = sIndex.data(Qt::DisplayRole).toString();

    QString arg = patchIndex < 0  ? tr("Directory") : tr("File");
    QString title = tr("Remove or discard %1?").arg(name);
    QString text =  tr("Are you sure you want to remove or discard all changes in '%1'?").arg(name);
    QMessageBox *dialog = new QMessageBox(
      QMessageBox::Warning, title, text.arg(name),
      QMessageBox::Cancel, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setInformativeText(tr("This action cannot be undone."));

    QPushButton *discard =
      dialog->addButton(tr("Discard"), QMessageBox::AcceptRole);
    connect(discard, &QPushButton::clicked, [this, m, sIndex] {
        if (!m->discard(sIndex)) {
            QString patchName = sIndex.data(Qt::DisplayRole).toString();
            RepoView *view = RepoView::parentView(this);
            assert(view);
            LogEntry *parent = view->addLogEntry(patchName, tr("Discard"));
            view->error(parent, tr("discard"), patchName);
        }
    });
}

void TreeView::onCustomContextMenu(const QPointF& point)
{
    auto proxy = qobject_cast<TreeProxy*>(model());
    if (!proxy)
        return;


    QPoint p(qRound(point.x()), qRound(point.y()));
    QModelIndex index = indexAt(p);
    if (!index.isValid())
        return;

    QMenu contextMenu;
    QAction a;
//    if (proxy->staged()) {
//        a.setText(tr("Unstage selected"));

//    } else {
//        a.setText(tr("Stage selected"));
//    }
//    contextMenu.addAction(&a);
    auto discardAction = QAction(tr("Discard selected"));
    contextMenu.addAction(&discardAction);
    connect(&discardAction, &QAction::triggered, [this, index]() {
        this->discard(index);
    });

    contextMenu.exec(viewport()->mapToGlobal(p));


}

bool TreeView::eventFilter(QObject *obj, QEvent *event)
{
  if (event->type() == QEvent::MouseButtonPress) {
	QWidget *TreeViewport = static_cast<QWidget *>(obj);
    QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPos();
    QModelIndex index = indexAt(viewport()->mapFromGlobal(globalPos));
	if (!TreeViewport->hasFocus() && index.row() < 0) {
	  TreeViewport->setFocus();
      selectionModel()->clearSelection();
    }
  }

  return false;
}

void TreeView::handleSelectionChange(
  const QItemSelection &selected,
  const QItemSelection &deselected)
{
  // FIXME: The argument sent by Qt doesn't contain the whole selection.
  QModelIndexList indexes = selectionModel()->selectedIndexes();
  if (indexes.length() > 0) {
	  QModelIndex index = (indexes.size() == 1) ? indexes.first() : QModelIndex();
	  emit fileSelected(index);
  }

  // ignore deselection handling, because when selecting an item in the second
  // TreeView (staged/unstaged files), the root should not be set selected. Anything
  // should be selected in this View
  if (suppressDeselectionHandling)
	  return;

  // Handle deselection.
  if (indexes.isEmpty() && !deselected.indexes().isEmpty()) {
    QModelIndex parent = deselected.indexes().first().parent();
    setCurrentIndex(parent);
    if (!parent.isValid())
      setRootIndex(QModelIndex());
  }
}

void TreeView::setCollapseCount(int value)
{
    assert(value >= 0);
    mCollapseCount = value;
    emit collapseCountChanged(mCollapseCount);
}

void TreeView::updateCollapseCount(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles)
{
    assert(topLeft == bottomRight); // makes no sense that they are different here. See also the TreeModel

    if (roles[0] != Qt::CheckStateRole)
        return;

    setCollapseCount(countCollapsed());

}

void TreeView::updateCollapseCount(const QModelIndex &parent, int first, int last)
{
    setCollapseCount(countCollapsed());
}

/*!
 * \brief TreeView::countCollapsed
 * Takes long for many items
 * \param parent
 * \return
 */
int TreeView::countCollapsed(QModelIndex parent, bool recursive)
{
    QAbstractItemModel* model = this->model();

    int count = 0;
    for (int i=0; i < model->rowCount(parent); i++) {
        QModelIndex idx = model->index(i, 0, parent);
        if (model->rowCount(idx) && !this->isExpanded(idx))
            count++;
        if (recursive)
            count += countCollapsed(idx);
    }
    return count;
}

void TreeView::expandAll()
{
    mSupressItemExpandStateChanged = true;
    QTreeView::expandAll();
    mSupressItemExpandStateChanged = false;
    setCollapseCount(0);
}

void TreeView::collapseAll()
{
    mSupressItemExpandStateChanged = true;
    QTreeView::collapseAll();
    mSupressItemExpandStateChanged = false;
    setCollapseCount(model()->rowCount());
}

void TreeView::itemExpanded(const QModelIndex& index)
{
    if (mSupressItemExpandStateChanged)
        return;

    setCollapseCount(mCollapseCount - 1 + countCollapsed(index, false));
}

void TreeView::itemCollapsed(const QModelIndex& index)
{
    if (mSupressItemExpandStateChanged)
        return;

    setCollapseCount(mCollapseCount + 1 - countCollapsed(index, false));
}

void TreeView::deselectAll() {
	suppressDeselectionHandling = true;
	selectionModel()->clearSelection();
	suppressDeselectionHandling = false;
}
