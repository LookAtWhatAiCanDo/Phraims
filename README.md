Qt6 Splitter Hello
==================

Simple Qt6 Widgets demo that divides the main area into N equal sections.

Build (requires Qt6 development libraries and a working CMake):

```bash
# from repository root
mkdir -p prototypes/qt-splitter/build && cd prototypes/qt-splitter/build
cmake ..
cmake --build . --config Release
./QtSplitterHello
```

Controls:
- Use the + / - toolbar buttons or the spinbox to change the number of sections.
- Each section is equally sized using layout stretch factors.
