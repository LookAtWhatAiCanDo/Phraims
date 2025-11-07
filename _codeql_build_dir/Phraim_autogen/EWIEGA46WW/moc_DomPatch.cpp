/****************************************************************************
** Meta object code from reading C++ file 'DomPatch.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../DomPatch.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DomPatch.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_DomPatchesDialog_t {
    uint offsetsAndSizes[20];
    char stringdata0[17];
    char stringdata1[9];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[7];
    char stringdata5[9];
    char stringdata6[16];
    char stringdata7[9];
    char stringdata8[5];
    char stringdata9[6];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_DomPatchesDialog_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_DomPatchesDialog_t qt_meta_stringdata_DomPatchesDialog = {
    {
        QT_MOC_LITERAL(0, 16),  // "DomPatchesDialog"
        QT_MOC_LITERAL(17, 8),  // "loadList"
        QT_MOC_LITERAL(26, 0),  // ""
        QT_MOC_LITERAL(27, 5),  // "onAdd"
        QT_MOC_LITERAL(33, 6),  // "onEdit"
        QT_MOC_LITERAL(40, 8),  // "onDelete"
        QT_MOC_LITERAL(49, 15),  // "editPatchDialog"
        QT_MOC_LITERAL(65, 8),  // "DomPatch"
        QT_MOC_LITERAL(74, 4),  // "p_in"
        QT_MOC_LITERAL(79, 5)   // "isNew"
    },
    "DomPatchesDialog",
    "loadList",
    "",
    "onAdd",
    "onEdit",
    "onDelete",
    "editPatchDialog",
    "DomPatch",
    "p_in",
    "isNew"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_DomPatchesDialog[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,   44,    2, 0x08,    1 /* Private */,
       3,    0,   45,    2, 0x08,    2 /* Private */,
       4,    0,   46,    2, 0x08,    3 /* Private */,
       5,    0,   47,    2, 0x08,    4 /* Private */,
       6,    2,   48,    2, 0x08,    5 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 7, QMetaType::Bool,    8,    9,

       0        // eod
};

Q_CONSTINIT const QMetaObject DomPatchesDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_meta_stringdata_DomPatchesDialog.offsetsAndSizes,
    qt_meta_data_DomPatchesDialog,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_DomPatchesDialog_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DomPatchesDialog, std::true_type>,
        // method 'loadList'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onAdd'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onEdit'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDelete'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'editPatchDialog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const DomPatch &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void DomPatchesDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DomPatchesDialog *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->loadList(); break;
        case 1: _t->onAdd(); break;
        case 2: _t->onEdit(); break;
        case 3: _t->onDelete(); break;
        case 4: _t->editPatchDialog((*reinterpret_cast< std::add_pointer_t<DomPatch>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject *DomPatchesDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DomPatchesDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_DomPatchesDialog.stringdata0))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int DomPatchesDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
