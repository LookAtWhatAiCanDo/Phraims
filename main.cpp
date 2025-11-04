#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QSpinBox>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QStyle>
#include <QPalette>

// Simple Qt6 Widgets app that divides the main area into N equal sections.
// The user controls the number of sections with + / - buttons or the spinbox.

class SplitWindow : public QMainWindow {
  Q_OBJECT

 public:
  SplitWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
    setWindowTitle("Qt6 Splitter Hello");
    resize(800, 600);

    // Toolbar with controls
    auto *tb = addToolBar("controls");
    tb->setMovable(false);

    QAction *dec = tb->addAction(style()->standardIcon(QStyle::SP_ArrowLeft), "-");
    QAction *inc = tb->addAction(style()->standardIcon(QStyle::SP_ArrowRight), "+");

    tb->addSeparator();
    tb->addWidget(new QLabel("Sections:"));
    spin_ = new QSpinBox();
    spin_->setRange(1, 64);
    spin_->setValue(3); // default
    tb->addWidget(spin_);

    // central scroll area to allow many sections
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    central_ = new QWidget();
    scroll->setWidget(central_);
    setCentralWidget(scroll);

    layout_ = new QVBoxLayout(central_);
    layout_->setContentsMargins(4, 4, 4, 4);
    layout_->setSpacing(6);

    connect(inc, &QAction::triggered, this, [this]() { spin_->setValue(spin_->value() + 1); });
    connect(dec, &QAction::triggered, this, [this]() { spin_->setValue(spin_->value() - 1); });
    connect(spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SplitWindow::rebuildSections);

    rebuildSections(spin_->value());
  }

 private slots:
  void rebuildSections(int n) {
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
      auto *frame = new QFrame();
      frame->setFrameShape(QFrame::StyledPanel);
      frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

      // alternating subtle background colors for clarity
      QPalette pal = frame->palette();
      QColor base = palette().color(QPalette::Window);
      int shift = (i % 2 == 0) ? 6 : -6;
      QColor bg = base.lighter(100 + shift);
      pal.setColor(QPalette::Window, bg);
      frame->setAutoFillBackground(true);
      frame->setPalette(pal);

      auto *label = new QLabel(QString("Section %1").arg(i + 1));
      label->setAlignment(Qt::AlignCenter);
      auto *innerLayout = new QVBoxLayout(frame);
      innerLayout->addStretch(1);
      innerLayout->addWidget(label);
      innerLayout->addStretch(2);

      layout_->addWidget(frame, 1); // stretch=1 -> equal share
    }

    // add a final stretch with zero so that widgets entirely control spacing
    layout_->addStretch(0);
    central_->update();
  }

 private:
  QWidget *central_ = nullptr;
  QVBoxLayout *layout_ = nullptr;
  QSpinBox *spin_ = nullptr;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  SplitWindow w;
  w.show();

  return app.exec();
}

#include "main.moc"
