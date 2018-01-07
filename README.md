# Project Title

Master thesis on a sketch-based 3D modeling system for VR.

## Getting Started

### Prerequisites

Tested on Windows 10 with Microsoft Visual Studio 17 (with Visual C++) and OS X El Capitan with clang 8.0.0.

### Installing

Clone the repository including submodules (this contains an adapted version of libigl that is necessary) with 

```
git clone --recursive https://github.com/FloorVerhoeven/thesis.git
```

Create a build folder in the "SketchMesh" folder.
Use CMake to build the binaries for your system. You can either do this via the CMake GUI (press configure and then generate) or via the command line with navigating into the build folder and running:

```
cmake -DCMAKE_BUILD_TYPE=Release ../
```

Then, from inside the build folder run the following to compile and run the executable:
```
make && ./SketchMesh_bin
```
This will launch the application.

## Usage

* For drawing a initial mesh, press 'D' and draw the shape (while pressing the mouse down). Slower drawing results in more internal triangles but also a slower processing. The mesh can then be inflated by repeatedly clicking the "perform 1 smoothing iteration"-button in the menu on the left hand side.
* For adding additional control curves on the existing mesh surface, press 'A' and draw the stroke (while pressing the mouse down). Strokes that start and end outside of the mesh surface will be interpreted as a looped stroke that also goes around the backside of the mesh. For strokes that start outside of the mesh but end inside it, only the points that were drawn on the mesh will be considered.
* For pulling (deforming) the shape, press 'P' and click close to one of the vertices of the stroke that you want to pull on, and while holding the mouse button down, pull to the desired position. The menu on the left hand side allows for switching between smooth and sharp deformation. If you click too far away from one of the stroke vertices, the mouse movement will be interpreted as a navigation movement.
* For removing additional control curves, press 'R' and click close to one of the vertices of the stroke that you want to remove. Its vertices will turn black and you need to click one of the vertices of the same curve again to confirm the removal.