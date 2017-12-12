#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif
#include <stdlib.h>
#include <iostream>
#include <igl/readOFF.h>
#include <igl/viewer/Viewer.h>
#include <igl/vertex_triangle_adjacency.h>
#include <igl/adjacency_list.h>
#include <igl/per_face_normals.h>
#include <igl/per_vertex_normals.h>
#include <igl/per_corner_normals.h>
#include <igl/facet_components.h>
#include <igl/jet.h>
#include <igl/barycenter.h>
#include <igl/cat.h>
#include <cmath>
#include <igl/triangle_triangle_adjacency.h>
#include "SketchMesh.h"
#include "Stroke.h"
#include "SurfaceSmoothing.h"
#include "CurveDeformation.h"


using namespace std;
using Viewer = igl::viewer::Viewer;

// Vertex array, #V x3
Eigen::MatrixXd V;
// Face array, #F x3
Eigen::MatrixXi F;
// Per-face normal array, #F x3
Eigen::MatrixXd FN;
// Per-vertex normal array, #V x3
Eigen::MatrixXd VN;
// Per-corner normal array, (3#F) x3
Eigen::MatrixXd CN;
// Vectors of indices for adjacency relations
std::vector<std::vector<int> > VF, VFi, VV;
// Integer vector of component IDs per face, #F x1
Eigen::VectorXi cid;
// Per-face color array, #F x3
Eigen::MatrixXd component_colors_per_face;

// Per vertex indicator of whether vertex is on boundary (on boundary if == 1)
Eigen::VectorXi vertex_boundary_markers;
//Per vertex indicator of whether vertex is on original stroke (outline of shape) (on OG stroke if ==1)
Eigen::VectorXi part_of_original_stroke;

//Mouse interaction
enum ToolMode{DRAW, ADD, CUT, EXTRUDE, PULL, REMOVE, CHANGE, SMOOTH, NAVIGATE, NONE};
ToolMode tool_mode = NAVIGATE;
bool skip_standardcallback = false;
int down_mouse_x = -1, down_mouse_y = -1;

double vertex_weights;
int smooth_iter = 1;
bool mouse_is_down = false; //We need this due to mouse_down not working in the nanogui menu, whilst mouse_up does work there
bool mouse_has_moved = false;

//For selecting vertices
Stroke* initial_stroke;
Stroke* added_stroke;
vector<Stroke> stroke_collection;
int handleID = -1;

int turnNr = 0;
bool dirty_boundary = false;
int closest_stroke_ID;
int next_added_stroke_ID = 2;

bool callback_key_down(Viewer& viewer, unsigned char key, int modifiers) {
	if (key == '1') {
		viewer.data.clear();
	}else if (key == 'D') { //use capital letters
		//Draw initial curve/mesh
		tool_mode = DRAW;
	}else if (key == 'P') {
		tool_mode = PULL;
	}else if (key == 'N') {
		//Use navigation
		tool_mode = NAVIGATE;
	} else if(key == 'A') {
		//Add an extra control curve to an existing mesh
		if(initial_stroke->empty2D()){ //Don't go into "additional curve mode" if there is no mesh yet
			return true;
		}
		tool_mode = ADD;
	}

	//viewer.ngui->refresh(); //TODO: is this needed?
	return true;
}

bool callback_mouse_down(Viewer& viewer, int button, int modifier) {
	if (button == (int)Viewer::MouseButton::Right) {
		return false;
	}
	mouse_is_down = true;

	down_mouse_x = viewer.current_mouse_x;
	down_mouse_y = viewer.current_mouse_y;

	if (tool_mode == DRAW) { //Creating the first curve/mesh
		viewer.data.clear();
		stroke_collection.clear();
		initial_stroke->strokeReset();
		initial_stroke->strokeAddSegment(down_mouse_x, down_mouse_y);
		skip_standardcallback = true;
	} else if(tool_mode == ADD) { //Adding a new control curve onto an existing mesh
		added_stroke = new Stroke(V, F, viewer, next_added_stroke_ID);
		next_added_stroke_ID++;
		skip_standardcallback = added_stroke->strokeAddSegmentAdd(down_mouse_x, down_mouse_y); //If the user starts outside of the mesh, consider the movement as navigation
	} else if (tool_mode == PULL) { //Dragging an existing curve
		double closest_dist = INFINITY;
		handleID = initial_stroke->selectClosestVertex(down_mouse_x, down_mouse_y, closest_dist);
		double current_closest = closest_dist;
		closest_stroke_ID = -1;

		for(int i = 0; i < stroke_collection.size(); i++) {
			int tmp_handleID = stroke_collection[i].selectClosestVertex(down_mouse_x, down_mouse_y, closest_dist);
			if((closest_dist < current_closest) && (tmp_handleID != -1)) {
				current_closest = closest_dist;
				handleID = tmp_handleID;
				closest_stroke_ID = i;
			}
		}

		if(handleID == -1) {//User clicked too far from any of the stroke vertices
			return false;
		}
		if(closest_stroke_ID == -1) {
			CurveDeformation::startPullCurve(*initial_stroke, handleID, V.rows());
		} else {
			CurveDeformation::startPullCurve(stroke_collection[closest_stroke_ID], handleID, V.rows());
		}
		skip_standardcallback = true;
	}
	else if (tool_mode == NAVIGATE) { //Navigate through the screen
		skip_standardcallback = false; //We do want to use the navigation functionality
	}
	else if(tool_mode == EXTRUDE) {
		initial_stroke->strokeAddSegmentExtrusion(down_mouse_x, down_mouse_y);
		skip_standardcallback = true;
	}


	return skip_standardcallback; //Will make sure that we use standard navigation responses if we didn't do special actions and vice versa
}

bool callback_mouse_move(Viewer& viewer, int mouse_x, int mouse_y) {
	if (!skip_standardcallback) {
		return false;
	}

	if(viewer.down) { //Only consider it to be moving if the button was held down
		mouse_has_moved = true;
	}


	if (tool_mode == DRAW && viewer.down) { //If we're still holding the mouse down
		initial_stroke->strokeAddSegment(mouse_x, mouse_y);
		return true;
	} else if(tool_mode == ADD && viewer.down) {
		bool success = added_stroke->strokeAddSegmentAdd(mouse_x, mouse_y);
		if(success) {
			mouse_has_moved = true;
		} else {
			mouse_has_moved = false;
		}
		return success;
	} else if(tool_mode == EXTRUDE && viewer.down) {
		initial_stroke->strokeAddSegmentExtrusion(mouse_x, mouse_y);
		return true;
	} else if(tool_mode == PULL && viewer.down && handleID != -1) {
		double x = mouse_x;
		double y = viewer.core.viewport(3) - mouse_y;
		
		Eigen::Matrix4f modelview = viewer.core.view * viewer.core.model;
		int global_handleID;
		if(closest_stroke_ID == -1) {
			global_handleID = initial_stroke->get_vertex_idx_for_point(handleID);
		} else {
			global_handleID = stroke_collection[closest_stroke_ID].get_vertex_idx_for_point(handleID);
		}
		Eigen::RowVector3f pt1(viewer.data.V(global_handleID, 0), viewer.data.V(global_handleID, 1), viewer.data.V(global_handleID, 2));
		Eigen::RowVector3f pr;
		igl::project(pt1, modelview, viewer.core.proj, viewer.core.viewport, pr);
		Eigen::RowVector3d pt = igl::unproject(Eigen::Vector3f(x, y, pr[2]), modelview, viewer.core.proj, viewer.core.viewport).transpose().cast<double>();

		if(turnNr == 0) { //increase the number to smooth less often
			CurveDeformation::pullCurve(pt, V);
			if(dirty_boundary) { //Smooth an extra time if the boundary is dirty, because smoothing once with a dirty boundary results in a flat mesh
				for(int i = 0; i < 2; i++) {
					SurfaceSmoothing::smooth(V, F, vertex_boundary_markers, part_of_original_stroke, dirty_boundary);
				}
			}
			SurfaceSmoothing::smooth(V, F, vertex_boundary_markers, part_of_original_stroke, dirty_boundary);

			turnNr++;
		} else {
			turnNr++;
			if(turnNr == 4) {
				turnNr = 0;
			}
		}
		initial_stroke->update_Positions(V);
		for(int i = 0; i < stroke_collection.size(); i++) {
			stroke_collection[i].update_Positions(V);
		}

		viewer.data.set_mesh(V, F);
		viewer.data.compute_normals();

		Eigen::MatrixXd init_points = initial_stroke->get3DPoints();
		viewer.data.set_points(init_points.topRows(init_points.rows()-1), Eigen::RowVector3d(1, 0, 0));
		viewer.data.set_stroke_points(init_points);

		viewer.data.set_edges(Eigen::MatrixXd(), Eigen::MatrixXi(), Eigen::RowVector3d(0, 0, 1));
		Eigen::MatrixXd added_points;
		for(int i = 0; i < stroke_collection.size(); i++) {
			added_points = stroke_collection[i].get3DPoints();
			viewer.data.add_points(added_points.topRows(added_points.rows()-1), Eigen::RowVector3d(0, 0, 1));
			viewer.data.add_edges(added_points.block(0, 0, added_points.rows() - 2, 3), added_points.block(1, 0, added_points.rows() - 2, 3), Eigen::RowVector3d(0, 0, 1));
		}

		return true;
	}
	return false;
}

bool callback_mouse_up(Viewer& viewer, int button, int modifier) {
	if(!mouse_is_down) {
		return true;
	}
	mouse_is_down = false;

	if(tool_mode == DRAW) {
		if(initial_stroke->toLoop()) {//Returns false if the stroke only consists of 1 point (user just clicked)
            //Give some time to show the stroke
            #ifdef _WIN32
                Sleep(200);
            #else
                usleep(200000);  /* sleep for 200 milliSeconds */
            #endif
            initial_stroke->generate3DMeshFromStroke(vertex_boundary_markers, part_of_original_stroke);
			F = viewer.data.F;
			V = viewer.data.V;

			dirty_boundary = true;

			for(int i = 0; i < smooth_iter; i++) {
                SurfaceSmoothing::smooth(V, F, vertex_boundary_markers, part_of_original_stroke, dirty_boundary);
            }


			viewer.data.set_mesh(V, F);
            viewer.data.compute_normals();

            //Overlay the drawn stroke
			int strokeSize = (vertex_boundary_markers.array() > 0).count();
			Eigen::MatrixXd strokePoints = V.block(0, 0, strokeSize, 3);
			viewer.data.set_points(strokePoints, Eigen::RowVector3d(1, 0, 0)); //Displays dots
			viewer.data.set_stroke_points(igl::cat(1, strokePoints, (Eigen::MatrixXd) V.row(0)));

		}
		skip_standardcallback = false;
	} 
	else if(tool_mode == ADD && mouse_has_moved) {
		dirty_boundary = true;
		added_stroke->snap_to_vertices(vertex_boundary_markers);
		stroke_collection.push_back(*added_stroke);

		viewer.data.set_edges(Eigen::MatrixXd(), Eigen::MatrixXi(), Eigen::RowVector3d(0,0,1));
		Eigen::MatrixXd added_points;
		for(int i = 0; i < stroke_collection.size(); i++) {
			added_points = stroke_collection[i].get3DPoints();
			viewer.data.add_points(added_points, Eigen::RowVector3d(0, 0, 1));
			viewer.data.add_edges(added_points.block(0, 0, added_points.rows() - 2, 3), added_points.block(1, 0, added_points.rows() - 2, 3), Eigen::RowVector3d(0, 0, 1));
		}
	}
	else if(tool_mode == PULL && handleID != -1 && mouse_has_moved) {
		for(int i = 0; i < smooth_iter; i++) {
            SurfaceSmoothing::smooth(V, F, vertex_boundary_markers, part_of_original_stroke, dirty_boundary);
		}

		for(int i = 0; i < stroke_collection.size(); i++) {
			stroke_collection[i].update_Positions(V);
		}

		viewer.data.set_mesh(V, F);
		viewer.data.compute_normals();

		//Overlay the updated stroke
		Eigen::MatrixXd init_points = initial_stroke->get3DPoints();
		viewer.data.set_points(init_points.topRows(init_points.rows() - 1), Eigen::RowVector3d(1, 0, 0));
		viewer.data.set_stroke_points(init_points);

		viewer.data.set_edges(Eigen::MatrixXd(), Eigen::MatrixXi(), Eigen::RowVector3d(0, 0, 1));
		Eigen::MatrixXd added_points;
		for(int i = 0; i < stroke_collection.size(); i++) {
			added_points = stroke_collection[i].get3DPoints();
			viewer.data.add_points(added_points.topRows(added_points.rows() - 1), Eigen::RowVector3d(0, 0, 1));
			viewer.data.add_edges(added_points.block(0, 0, added_points.rows() - 2, 3), added_points.block(1, 0, added_points.rows() - 2, 3), Eigen::RowVector3d(0, 0, 1));
		}

	}

	mouse_has_moved = false;
	return skip_standardcallback;
}

//TODO: make callback for this in viewer, like in exercise 5 of shapemod
bool callback_load_mesh(Viewer& viewer, string filename)
{
	igl::readOFF(filename, V, F);
	viewer.data.clear();
	viewer.data.set_mesh(V, F);
	viewer.data.compute_normals();
	viewer.core.align_camera_center(viewer.data.V);

	std::cout << filename.substr(filename.find_last_of("/") + 1) << endl;
	return true;
}

int main(int argc, char *argv[]) {
	// Show the mesh
	Viewer viewer;
	viewer.callback_key_down = callback_key_down;
	viewer.callback_mouse_down = callback_mouse_down;
	viewer.callback_mouse_move = callback_mouse_move;
	viewer.callback_mouse_up = callback_mouse_up;
	viewer.core.point_size = 15;
	//viewer.callback_load_mesh = callback_load_mesh;
    
    viewer.callback_init = [&](igl::viewer::Viewer& viewer)
    {
        // Add new group
        viewer.ngui->addGroup("Inflation");
        
        // Expose a variable directly ...
        viewer.ngui->addVariable("Vertex Weights",SurfaceSmoothing::vertex_weight);
        viewer.ngui->addVariable("Edge Weights",SurfaceSmoothing::edge_weight);

        
        // Expose a variable directly ...
        viewer.ngui->addVariable("Smoothing iterations",smooth_iter);

        
        // Add a button
        viewer.ngui->addButton("Perform 1 smoothing iteration",[&viewer](){
            SurfaceSmoothing::smooth(V, F, vertex_boundary_markers, part_of_original_stroke, dirty_boundary);
            viewer.data.set_mesh(V, F);
            viewer.data.compute_normals();
        });

		viewer.ngui->addGroup("Curve deformation");
		viewer.ngui->addVariable<bool>("Smooth deformation", CurveDeformation::smooth_deform_mode);
        
        // call to generate menu
        viewer.screen->performLayout();
        return false;
    };

	//Init stroke selector
	initial_stroke = new Stroke(V, F, viewer, 0);
	if (argc == 2)
	{
		// Read mesh
		igl::readOFF(argv[1], V, F);
	//	callback_load_mesh(viewer, argv[1]);
	}
	else
	{
		// Read mesh
		//callback_load_mesh(viewer, "../data/cube.off");
	}

	callback_key_down(viewer, '1', 0);

	//viewer.core.align_camera_center(V);
	viewer.launch();
}
