#pragma once

#include <QObject>
#include <QEvent>
#include <QMouseEvent>
#include <QSplitter>

/**
 * @brief Event filter to handle double-click on splitter handles for equal resizing.
 *
 * This filter detects double-clicks on QSplitter handles and automatically
 * resizes the two adjacent widgets to equal sizes. This provides a quick
 * way to reset custom splitter positions to a balanced 50/50 split.
 *
 * Usage:
 * @code
 * QSplitter *splitter = new QSplitter();
 * SplitterDoubleClickFilter *filter = new SplitterDoubleClickFilter(splitter);
 * splitter->installEventFilter(filter);
 * @endcode
 */
class SplitterDoubleClickFilter : public QObject {
  Q_OBJECT

public:
  /**
   * @brief Constructs a splitter double-click filter.
   * @param parent Parent object (typically the QSplitter being filtered)
   */
  explicit SplitterDoubleClickFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
  /**
   * @brief Filters events to detect double-clicks on splitter handles.
   * @param obj The object being watched
   * @param event The event being filtered
   * @return true if the event should be filtered (not propagated), false otherwise
   *
   * When a double-click is detected on a splitter handle, this method calculates
   * the total available space and distributes it equally between the two widgets
   * adjacent to the clicked handle. The equal sizes are immediately applied and
   * will be persisted through the normal save mechanism.
   */
  bool eventFilter(QObject *obj, QEvent *event) override {
    if (event->type() == QEvent::MouseButtonDblClick) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        QSplitter *splitter = qobject_cast<QSplitter *>(obj);
        if (splitter) {
          // Find which handle was clicked by checking the splitter handle at the mouse position
          int handleIndex = -1;
          for (int i = 0; i < splitter->count() - 1; ++i) {
            QSplitterHandle *handle = splitter->handle(i + 1);
            if (handle) {
              QPoint handlePos = handle->mapFrom(splitter, mouseEvent->pos());
              if (handle->rect().contains(handlePos)) {
                handleIndex = i;
                break;
              }
            }
          }

          if (handleIndex >= 0 && handleIndex < splitter->count() - 1) {
            // Get the current sizes of all widgets
            QList<int> sizes = splitter->sizes();
            
            // Calculate the total space for the two adjacent widgets
            int totalSize = sizes[handleIndex] + sizes[handleIndex + 1];
            
            // Distribute equally
            int halfSize = totalSize / 2;
            sizes[handleIndex] = halfSize;
            sizes[handleIndex + 1] = totalSize - halfSize;  // Use remainder to avoid rounding issues
            
            // Apply the new sizes
            splitter->setSizes(sizes);
            
            // Emit signal to notify that sizes changed (for persistence)
            emit splitterResized();
            
            // Return true to indicate we handled this event
            return true;
          }
        }
      }
    }
    return QObject::eventFilter(obj, event);
  }

signals:
  /**
   * @brief Emitted when the splitter is resized via double-click.
   *
   * This signal can be used by the parent window to trigger persistence
   * of the new splitter sizes.
   */
  void splitterResized();
};
