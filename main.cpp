#include "mainwindow.h"
#include "WLog.h"

#include <QApplication>
#include <QDebug>

#include <manifold/manifold.h>

int main(int argc, char *argv[])
{
    wlog::init_rotating_logger("zz.log");
    QApplication a(argc, argv);

    // Demonstrate that the manifold library (via vcpkg) is linked and usable.
    {
        const manifold::Manifold cube = manifold::Manifold::Cube({1.0, 1.0, 1.0}, true);
        qDebug() << "manifold: cube volume =" << cube.Volume()
                 << "surface area =" << cube.SurfaceArea();
    }

    MainWindow w;
    w.show();
    auto ret = a.exec();
    wlog::shutdown();
    return ret;
}
