#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLineEdit>
#include <vector>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QScrollArea>
#include <QPalette>
#include <QSettings>

// Simple Qt6 Widgets app that divides the main area into N equal sections.
// The user controls the number of sections with + / - buttons or the spinbox.

// A self-contained frame used for each split section. Contains a top
// address bar (QLineEdit) and a simple content area below.
class SplitFrameWidget : public QFrame {
  Q_OBJECT

 public:
  SplitFrameWidget(int index, QWidget *parent = nullptr) : QFrame(parent) {
    setFrameShape(QFrame::StyledPanel);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // subtle alternating background color based on index
    QPalette pal = palette();
    QColor base = palette().color(QPalette::Window);
    int shift = (index % 2 == 0) ? 6 : -6;
    QColor bg = base.lighter(100 + shift);
    pal.setColor(QPalette::Window, bg);
    setAutoFillBackground(true);
    setPalette(pal);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(6);

    // top row: address bar + +/- buttons
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(6);

    address_ = new QLineEdit(this);
    address_->setPlaceholderText("Address or URL");
    address_->setClearButtonEnabled(true);
    topRow->addWidget(address_, 1);

    plusBtn_ = new QToolButton(this);
    plusBtn_->setText("+");
    plusBtn_->setToolTip("Insert a new section after this one");
    topRow->addWidget(plusBtn_);

    minusBtn_ = new QToolButton(this);
    minusBtn_->setText("-");
    minusBtn_->setToolTip("Remove this section");
    topRow->addWidget(minusBtn_);

    v->addLayout(topRow);

    contentLabel_ = new QLabel(QString("Section %1").arg(index + 1), this);
    contentLabel_->setAlignment(Qt::AlignCenter);
    v->addStretch(1);
    v->addWidget(contentLabel_);
    v->addStretch(2);

    // wire internal UI to emit signals
    connect(plusBtn_, &QToolButton::clicked, this, [this]() { emit plusClicked(this); });
    connect(minusBtn_, &QToolButton::clicked, this, [this]() { emit minusClicked(this); });
    connect(address_, &QLineEdit::editingFinished, this, [this]() { emit addressEdited(this, address_->text()); });
  }

  QString address() const { return address_->text(); }
  void setAddress(const QString &s) { address_->setText(s); }

  void setMinusEnabled(bool en) { if (minusBtn_) minusBtn_->setEnabled(en); }

 signals:
  void plusClicked(SplitFrameWidget *who);
  void minusClicked(SplitFrameWidget *who);
  void addressEdited(SplitFrameWidget *who, const QString &text);

 private:
  QLineEdit *address_ = nullptr;
  QLabel *contentLabel_ = nullptr;
  QToolButton *plusBtn_ = nullptr;
  QToolButton *minusBtn_ = nullptr;
};


class SplitWindow : public QMainWindow {
  Q_OBJECT

 public:
  SplitWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
    setWindowTitle("Qt6 Splitter Hello");
    resize(800, 600);

    // No global toolbar; per-frame + / - buttons control sections.

    // central scroll area to allow many sections
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    central_ = new QWidget();
    scroll->setWidget(central_);
    setCentralWidget(scroll);

    layout_ = new QVBoxLayout(central_);
    layout_->setContentsMargins(4, 4, 4, 4);
    layout_->setSpacing(6);

    // load persisted section count (default 1)
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    int startCount = settings.value("sectionCount", 1).toInt();
    if (startCount < 1) startCount = 1;
    addresses_.clear();
    addresses_.resize(startCount);
    rebuildSections(startCount);
  }

 private slots:
  void rebuildSections(int n) {
    // Ensure addresses_ vector matches requested size, preserving existing values.
    if ((int)addresses_.size() < n) {
      addresses_.resize(n);
    } else if ((int)addresses_.size() > n) {
      addresses_.resize(n);
    }
    // clamp n
    if (n < 1) n = 1;

    // clear existing items
    QLayoutItem *child;
    while ((child = layout_->takeAt(0)) != nullptr) {
      if (auto *w = child->widget()) {
        w->hide();
        w->deleteLater();
      }
      delete child;
    }

    // create n equal sections. Use stretch=1 on each widget so they share space equally.
    for (int i = 0; i < n; ++i) {
      auto *frame = new SplitFrameWidget(i);
      // restore saved address if present
      frame->setAddress(addresses_[i]);

      // wire signals from the frame to the window handlers
      connect(frame, &SplitFrameWidget::plusClicked, this, &SplitWindow::onPlusFromFrame);
      connect(frame, &SplitFrameWidget::minusClicked, this, &SplitWindow::onMinusFromFrame);
      connect(frame, &SplitFrameWidget::addressEdited, this, &SplitWindow::onAddressEdited);

      // enable/disable minus button depending on how many sections
      frame->setMinusEnabled(n > 1);

      layout_->addWidget(frame, 1); // stretch=1 -> equal share
    }

    // add a final stretch with zero so that widgets entirely control spacing
    layout_->addStretch(0);
    central_->update();
  }

  void onPlusFromFrame(SplitFrameWidget *who) {
    // find index of the emitter within layout
    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
    if (pos < 0) return;

    addresses_.insert(addresses_.begin() + pos + 1, QString());
    // persist count
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    settings.setValue("sectionCount", (int)addresses_.size());
    // rebuild UI with the updated addresses_
    rebuildSections((int)addresses_.size());
  }

  void onMinusFromFrame(SplitFrameWidget *who) {
    if (addresses_.size() <= 1) return; // shouldn't remove last

    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
    if (pos < 0) return;

    addresses_.erase(addresses_.begin() + pos);
    // persist count
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    settings.setValue("sectionCount", (int)addresses_.size());
    rebuildSections((int)addresses_.size());
  }

  void onAddressEdited(SplitFrameWidget *who, const QString &text) {
    int pos = -1;
    int widgetIndex = 0;
    for (int i = 0; i < layout_->count(); ++i) {
      QLayoutItem *it = layout_->itemAt(i);
      QWidget *w = it ? it->widget() : nullptr;
      if (!w) continue;
      if (w == who) {
        pos = widgetIndex;
        break;
      }
      ++widgetIndex;
    }
    if (pos < 0) return;
    if (pos < (int)addresses_.size()) addresses_[pos] = text;
  }

  void closeEvent(QCloseEvent *event) override {
    // persist current section count on exit
    QSettings settings("NightVsKnight", "LiveStreamMultiChat");
    settings.setValue("sectionCount", (int)addresses_.size());
    QMainWindow::closeEvent(event);
  }

 private:
  QWidget *central_ = nullptr;
  QVBoxLayout *layout_ = nullptr;
  // QSpinBox removed; per-frame buttons control section count.
  std::vector<QString> addresses_;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  SplitWindow w;
  w.show();

  return app.exec();
}

#include "main.moc"
