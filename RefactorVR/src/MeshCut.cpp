#include "MeshCut.h"
#include "LaplacianRemesh.h"
#include <igl/triangle/triangulate.h>
#include <igl/slice.h>
#include <igl/edge_topology.h>

using namespace std;
using namespace igl;


bool MeshCut::cut(Mesh& m, Stroke& stroke, int clicked_face) {

	//Mesh m(V, F, vertex_boundary_markers, part_of_original_stroke, new_mapped_indices, sharp_edge, -1); //Give ID -1 since we're not using it here anyway
	SurfacePath surface_path;
	bool success = surface_path.create_from_stroke_cut(stroke); //Prepares the drawn stroke (inserts extra points at the edges that it crosses)
	if (!success) {
		return false;
	}
	success = cut_main(m, surface_path, stroke, clicked_face);
	if (!success) {
		return false;
	}

	post_cut_update_points(stroke, surface_path);

	return true;
}

bool MeshCut::cut_main(Mesh& m, SurfacePath& surface_path, Stroke& stroke, int clicked_face) {
	bool remesh_success = true;
	Eigen::VectorXi boundary_vertices = LaplacianRemesh::remesh_cut_remove_inside(m, surface_path, stroke.viewer.core.get_model(), stroke.viewer.oculusVR.get_start_action_view(), stroke.viewer.core.get_proj(), stroke.viewer.core.viewport, remesh_success, clicked_face);
	if (!remesh_success) {
		return false;
	}
	mesh_open_hole(boundary_vertices, m);

	return true;
}

void MeshCut::mesh_open_hole(Eigen::VectorXi& boundary_vertices, Mesh& m) {
	vector<int> sharp_edge_indices;
	for(int i = 0; i < m.sharp_edge.rows(); i++) {
		if(m.sharp_edge[i]) {
			sharp_edge_indices.push_back(i);
		}
	}

	Eigen::MatrixXi EV, FE, EF;
	igl::edge_topology(m.V, m.F, EV, FE, EF);

	//Keep only the sharp edges in the original mesh
	Eigen::MatrixXi sharpEV;
	Eigen::VectorXi sharpEV_row_idx, sharpEV_col_idx(2);
	sharpEV_row_idx = Eigen::VectorXi::Map(sharp_edge_indices.data(), sharp_edge_indices.size());
	sharpEV_col_idx.col(0) << 0, 1;
	igl::slice(EV, sharpEV_row_idx, sharpEV_col_idx, sharpEV); 

	//project points to 2D
	Eigen::MatrixXd boundary_vertices_2D(boundary_vertices.rows(), 2);
	Eigen::MatrixXi stroke_edges(boundary_vertices.rows(), 2);
	Eigen::RowVector3d center(0, 0, 0);
	Eigen::Vector3d x_vec, y_vec;
	double mean_squared_sample_dist = 0.0;
	project_points_to_2D(boundary_vertices, m, boundary_vertices_2D, stroke_edges, center, x_vec, y_vec, mean_squared_sample_dist);

	Eigen::MatrixXd V2;
	Eigen::MatrixXi F2, vertex_markers, edge_markers;
	//The 2D points are multiplied by a factor 1000 because otherwise triangulate runs out of precision. Multiply max. allowed triangle area with a factor because we need to accomodate for largest edge triangles in order to prevent the new surface from becoming concave during smoothing, and the average distance gets pulled down a lot because of edge-crossing sample parts.
	igl::triangle::triangulate(boundary_vertices_2D.leftCols(2), stroke_edges, Eigen::MatrixXd(0, 0), Eigen::MatrixXi::Constant(boundary_vertices_2D.rows(), 1, 1), Eigen::MatrixXi::Constant(stroke_edges.rows(), 1, 1), "QYq25a" + to_string(6*mean_squared_sample_dist), V2, F2, vertex_markers, edge_markers); //Capital Q silences triangle's output in cmd line. Also retrieves markers to indicate whether or not an edge/vertex is on the mesh boundary
	V2 /= 1000.0;

	int original_v_size = m.V.rows() - boundary_vertices.rows();		
	m.V.conservativeResize(original_v_size + V2.rows(), Eigen::NoChange);
	m.part_of_original_stroke.conservativeResize(original_v_size + V2.rows());
	m.vertex_boundary_markers.conservativeResize(original_v_size + V2.rows());

	//project back to 3D
	for (int i = 0; i < V2.rows(); i++) {
		if (i >= boundary_vertices.rows()) {//Interior vertices 
			Eigen::Vector3d v_tmp = center.transpose();
			v_tmp += x_vec*V2(i, 0);
			v_tmp += y_vec*V2(i, 1);
			m.V.row(original_v_size + i) << v_tmp.transpose(); 
			m.part_of_original_stroke[original_v_size + i] = 0;
			m.vertex_boundary_markers[original_v_size + i] = 0;
		}
	}

	update_face_indices(m, F2, boundary_vertices, original_v_size);
	update_sharp_edges(m, sharpEV);
}

void MeshCut::update_sharp_edges(Mesh& m, Eigen::MatrixXi& sharpEV) {
	Eigen::MatrixXi EV, FE, EF;
	igl::edge_topology(m.V, m.F, EV, FE, EF);
	m.sharp_edge.resize(EV.rows());
	m.sharp_edge.setZero();

	int start, end, equal_pos;
	Eigen::VectorXi col1Equals, col2Equals;
	for(int i = 0; i < sharpEV.rows(); i++) {
		start = sharpEV(i, 0);
		end = sharpEV(i, 1);
		if(start == -1 || end == -1) { //Sharp edge no longer exists
			continue;
		}

		col1Equals = EV.col(0).cwiseEqual(min(start, end)).cast<int>();
		col2Equals = EV.col(1).cwiseEqual(max(start, end)).cast<int>();
		(col1Equals + col2Equals).maxCoeff(&equal_pos); //Find the row that contains both vertices of this edge

		m.sharp_edge[equal_pos] = 1; //Set this edge to be sharp
	}
}

void MeshCut::update_face_indices(Mesh& m, Eigen::MatrixXi& F2, Eigen::VectorXi& boundary_vertices, int original_v_size) {
	int vert_idx_in_mesh, size_before = m.F.rows();
	m.F.conservativeResize(m.F.rows() + F2.rows(), Eigen::NoChange);
	for(int i = 0; i < F2.rows(); i++) {
		for(int j = 0; j < 3; j++) {
			if(F2(i, j) < boundary_vertices.rows()) { //Original boundary vertex
				vert_idx_in_mesh = boundary_vertices[F2(i, j)];
			} else { //Interior to cut plane
				vert_idx_in_mesh = F2(i, j) + original_v_size; //Add index to the currently existing number of mesh vertices (which already includes the boundary vertices on the cut stroke)
			}
			m.F(size_before + i, 2 - j) = vert_idx_in_mesh; //To ensure right face orientation after implementing extrusion
		}
	}
}

void MeshCut::project_points_to_2D(Eigen::VectorXi& boundary_vertices, Mesh& m, Eigen::MatrixXd& boundary_vertices_2D, Eigen::MatrixXi& stroke_edges, Eigen::RowVector3d& center, Eigen::Vector3d& x_vec, Eigen::Vector3d& y_vec, double& mean_squared_sample_dist) {
	for(int i = 0; i < boundary_vertices.rows(); i++) {
		center += m.V.row(boundary_vertices[i]);
	}
	center /= boundary_vertices.rows();

	Eigen::Vector3d normal(0, 0, 0);
	Eigen::Vector3d vec0, vec1;
	for(int i = 0; i < boundary_vertices.rows(); i++) {
		vec0 = m.V.row(boundary_vertices[i]) - center;
		vec1 = m.V.row(boundary_vertices[(i + 1) % boundary_vertices.rows()]) - center;
		normal += vec1.cross(vec0);
	}
	normal.normalize();

	x_vec = m.V.row(boundary_vertices[0]) - center;
	x_vec.normalize();
	y_vec = normal.cross(x_vec);

	Eigen::Vector3d vec;
	for(int i = 0; i < boundary_vertices.rows(); i++) {
		vec = 1000*(m.V.row(boundary_vertices[i]) - center);
		boundary_vertices_2D.row(i) << vec.dot(x_vec), vec.dot(y_vec);
		stroke_edges.row(i) << i, ((i + 1) % boundary_vertices.size());
	}

	for (int i = 0; i < boundary_vertices_2D.rows(); i++) {
		mean_squared_sample_dist += (boundary_vertices_2D.row(i) - boundary_vertices_2D.row((i + 1) % boundary_vertices_2D.rows())).squaredNorm();
	}
	mean_squared_sample_dist /= boundary_vertices_2D.rows();
}

//Updates the stroke's 3DPoints and closest_vert_bindings with the new vertices
void MeshCut::post_cut_update_points(Stroke& stroke, SurfacePath& surface_path) {
	vector<PathElement> path = surface_path.get_path();
	int nr_to_remove = 0;
	if (path[0].get_vertex() == path[path.size() - 1].get_vertex()) {
		nr_to_remove = 1;
	}

	Eigen::MatrixX3d new_3DPoints(path.size() + 1 - nr_to_remove, 3); //Increase size by 1 (if it's not a loop) because we want it to become a loop again
	vector<int> new_closest_vertex_indices(path.size() + 1 - nr_to_remove);
	for(int i = 0; i < path.size()-nr_to_remove; i++) {
		new_3DPoints.row(i) = path[i].get_vertex().transpose();
		new_closest_vertex_indices[i] = path[i].get_v_idx();
	}
	new_3DPoints.row(new_3DPoints.rows() - 1) = new_3DPoints.row(0);
	new_closest_vertex_indices[new_closest_vertex_indices.size() - 1] = new_closest_vertex_indices[0];

	stroke.set3DPoints(new_3DPoints);
	stroke.set_closest_vert_bindings(new_closest_vertex_indices);
}
