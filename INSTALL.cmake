MOOSE can also be built using cmake along with gnu-make. Instructions to build
MOOSE with gnu-make are described in file `INSTALL`.

# Building and installing moose

## Download the latest source code of moose from github or sourceforge.

    $ git clone -b master https://github.com/BhallaLab/moose 
    $ cd moose

## Install dependencies 

For moose-core:

- gsl-1.16 or higher.
- libsbml (optional)
- libhdf5 

For python module of MOOSE, following additional packages are required:

- Development package of python e.g. libpython-dev 
- python-numpy 

For python-gui, we need some more addtional packages
    
- matplotlib
- setuptools  (cmake uses it to install moose python extension and moogli)
- suds 
- Python bindings for Qt4 or higher
- Python OpenGL
- Python bindings for Qt's OpenGL module

On Ubuntu-120.4 or higher, these can be installed with:
    
    sudo apt-get install python-matplotlib python-qt4 python-qt4-gl 

## Use `cmake` to build moose:

    $ mkdir _build
    $ cd _build 
    $ cmake -DBUILD_MOOGLI=OFF -DWITH_DOC=OFF ..
    $ make 
    $ ctest --output-on-failure

This will build moose and its python extentions, `ctest` will run few tests to
check if build process was successful.

If you also want MOOGLI then pass `-DBUILD_MOOGLI=ON`.  Also see the section
__Building and installing moogli__ for more details about its dependencies.

To install MOOSE into non-standard directory, pass additional argument
`-DCMAKE_INSTALL_PREFIX=path/to/install/dir` to cmake.

After that installation is pretty easy.

## Install

    $ sudo make install

# Building and installing moogli 

MOOGLI is subproject of moogli for visualizing models. Details can be found
[here](http://moose.ncbs.res.in/moogli).

MOOGLI dependencies are huge! It uses `OpenSceneGraph` which has its own
dependencies. In nutshell, depending on your distribution, you would need
following packages to be installed.

- Development package of libopenscenegraph 
- [libQGLViewer-2.3.15-py](https://gforge.inria.fr/frs/?group_id=773). Install
instructions [here](http://www.libqglviewer.com//installUnix.html#linux)

- [PyQGLViewer0.10](https://gforge.inria.fr/frs/?group_id=773) (first install
libQGLViewer-2.3.15-py) and untar contents.

    $ cd / PyQGLViewer0.10
    $ python setup.py build # to compile
    $ python setup.py install # to install on your system
    $ python setup.py bdist # to create a binary distribution

On Ubuntu, following packages should suffice:

    $ sudo apt-get install python-qt4-dev python-qt4-gl libopenscenegraph-dev python-sip-dev
    libqt4-dev 
