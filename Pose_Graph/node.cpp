/*
 * node.cpp
 * 
 * Copyright 2015 Shibata-Lab <shibata-lab@shibatalab-X500H>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * NOTE: This is written according to ROS c++ guidelines
 */


#include <iostream>
#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <tf/transform_datatypes.h>
#include "sensor_msgs/PointCloud2.h"
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/conversions.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/filters/filter.h>
#include <pcl/filters/random_sample.h>
#include <pcl/registration/transforms.h> 
#include <pcl/registration/transformation_estimation_2D.h>
#include <pcl/registration/icp.h>
#include <Eigen/Dense>
#include <deque>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/graph/graphviz.hpp>

using namespace std;

class Node{
	public:
		float threshold_distance;
		ros::NodeHandle nh;
		ros::Subscriber odom;
		ros::Subscriber cloud;
		
		double pose_x,pose_y,roll,pitch,yaw;
		pcl::PCLPointCloud2::Ptr cloud_ptr;
		pcl::PCLPointCloud2 cloud_ ; 
		pcl::PointCloud<pcl::PointXYZ> curr_pc;
		Eigen::Matrix4f tr_mat;
		Eigen::Matrix4f prev_tr_mat;
		// properties for vertex of the graph 
		struct pose_{
			int key;
			std::vector<double>  data;
		};
		
		// Custom edge properties as constraints
		struct constraints_{
			int src;
			int obs;
			Eigen::Matrix4f transformation;
		};
		//Graph type 
		typedef boost::adjacency_list<boost::listS,boost::vecS, boost::undirectedS,pose_, constraints_> Graph;
		typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
		typedef boost::graph_traits<Graph>::edge_descriptor Edge;
		Graph gr;
		Graph::vertex_iterator vertex_It,vertex_End;
		Graph::edge_iterator edge_It,edge_End;
		
		Node(); //constructor
		/* Get Odometry data */
		void odomSub();
		
		/* procees odometry data to get 2D pose */
		void odomCallbk(nav_msgs::Odometry odom_data);
		
		/* Get Point Cloud data */
		void cloudSub();
		
		/* process point cloud data */ 
		void cloudCallbk(sensor_msgs::PointCloud2 msg);
		
		/* calculate Transform between two point clouds */ 
		Eigen::Matrix4f estTrans(pcl::PointCloud<pcl::PointXYZ> first,pcl::PointCloud<pcl::PointXYZ> second);	
		pcl::PointCloud<pcl::PointXYZ> randomSample(pcl::PointCloud<pcl::PointXYZ> in_cld);
	
		
		/* calculate distance travelled */ 
		float calculateDist(vector<double> curr_odom,vector<double> prev_odom);
		
		/* Add vertex to graph with given data */ 
		void addVertex(int v,std::vector<double> odom);
		
		/* Add edge to the last added vertex */ 
		void addEdge(Eigen::Matrix4f tr_msg);
		
		/* Initialize graph */ 
		void initGraph(int v,std::vector<double> odom);
		
		/* main node process */ 
		
	
	
	};
/* Callback function for odom subscriber */	
void Node::odomCallbk(nav_msgs::Odometry odom_data){
	/* Calculate quaternion to get roll, pitch , yaw */
	tf::Quaternion q(odom_data.pose.pose.orientation.x, odom_data.pose.pose.orientation.y, odom_data.pose.pose.orientation.z, odom_data.pose.pose.orientation.w);
	tf::Matrix3x3 m(q);
	pose_x = odom_data.pose.pose.position.x;
	pose_y = odom_data.pose.pose.position.y;
	m.getRPY(roll, pitch, yaw);
	}
	
/* Odometry subscriber function */
void Node::odomSub(){
	uint32_t queue_size = 1;
	odom = nh.subscribe<nav_msgs::Odometry>("/pose",queue_size,&Node::odomCallbk,this);
	}	

pcl::PointCloud<pcl::PointXYZ> Node::randomSample(pcl::PointCloud<pcl::PointXYZ> in_cld){
	// Randomly sample 1000 pts from the cloud to calculate 2d rigit transform
	pcl::PointCloud<pcl::PointXYZ>::Ptr in_cld_ptr (new pcl::PointCloud<pcl::PointXYZ>);
	*in_cld_ptr = in_cld;
	pcl::RandomSample<pcl::PointXYZ> sample(true);
	sample.setInputCloud(in_cld_ptr);
	sample.setSample(5000);  // 1000 pts
	std::vector<int> out_idx;
	sample.filter(out_idx);
	pcl::PointCloud<pcl::PointXYZ> out_cld;
	sample.filter(out_cld);
	std::cout<<out_cld.size()<<std::endl;
	return out_cld;
}

Eigen::Matrix4f Node::estTrans(pcl::PointCloud<pcl::PointXYZ> first,pcl::PointCloud<pcl::PointXYZ> second){
	/* Error that the size of both the cloud should be same
		Solution: randomly sample same number of points and get transform for those
			Estimating 2D transform between cloud points
	*/
	
	pcl::registration::TransformationEstimation2D< pcl::PointXYZ, pcl::PointXYZ, float >  ddTr; 	
	pcl::PointCloud<pcl::PointXYZ>::Ptr first_pc (new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PointCloud<pcl::PointXYZ>::Ptr second_pc (new pcl::PointCloud<pcl::PointXYZ>);
	*first_pc = this->randomSample(first);
	*second_pc = this->randomSample(second);
	 
	Eigen::Matrix4f result;
	ddTr.estimateRigidTransformation(*first_pc,*second_pc,result);
	//std::cout<<result<<std::endl;
	return result;
}
	
void Node::cloudCallbk(sensor_msgs::PointCloud2 msg){
	std::vector<int> nan_indices;
	pcl_conversions::toPCL(msg,cloud_);
	pcl::fromPCLPointCloud2(cloud_,curr_pc);
	pcl::removeNaNFromPointCloud(curr_pc,curr_pc,nan_indices);
	
	}
void Node::cloudSub(){
	uint32_t queue_size = 1;
	cloud = nh.subscribe<sensor_msgs::PointCloud2>("/camera/depth/points",queue_size,&Node::cloudCallbk, this);
	}


/* calculate distance travelled */ 
float Node::calculateDist(vector<double> curr_odom,vector<double> prev_odom){
	float dist;	
	dist = pow((curr_odom[0] - prev_odom[0]),2) + pow((curr_odom[1]-prev_odom[1]),2);
	dist = sqrt(dist);
	return dist;
	}
		
/* Add vertex to graph with given data */ 
void Node::addVertex(int v,std::vector<double> odom){
	Vertex v1;
	v1 = boost::add_vertex(gr);
	gr[v1].key = v ;
	gr[v1].data = odom;
	return;
	}
		
/* Add edge to the last added vertex */ 
void Node::addEdge(Eigen::Matrix4f tr_msg){
	//Graph::vertex_iterator vertex_It,vertex_End;
	boost::tie(vertex_It, vertex_End) = boost::vertices(gr);
	//std::cout<< "-->Vertex End is :"<<*vertexEnd<<std::endl;
	Edge e1; 
	e1 = (boost::add_edge(*(vertex_End-1),(*vertex_End),gr)).first;
	gr[e1].transformation = tr_msg;
	gr[e1].src = gr[*vertex_End-1].key;
	gr[e1].obs = gr[*vertex_End].key;
	}
		
/* Initialize graph */ 
void Node::initGraph(int v,std::vector<double> odom){
	Vertex v1;
	v1 = boost::add_vertex(gr);
	gr[v1].key = v ;
	gr[v1].data = odom;
	}
		
/* main node process */ 
Node::Node(){
	pcl::PointCloud<pcl::PointXYZ> prev_cld; // one step buffer for clouds comparision
	int count = 1; // conter for key of the vertex
	threshold_distance=0.5; // thesholding for the distance travelled
	/* Subscribe to odometry */ 
	this->odomSub(); // data-> pose_x,pose_y,roll, pitch, yaw
	/* Subscribe to point cloud */
	this->cloudSub(); //data-> curr_pc
	
	/* Initialize graph with first vertex as the starting point */
	std::vector<double> curr_odom;
	curr_odom.push_back(pose_x);
	curr_odom.push_back(pose_y);
	curr_odom.push_back(yaw);
	this->initGraph(0,curr_odom); //data-> gr graph
	
	/* Save the first cloud */ 
	prev_cld = curr_pc;
	/* Whle the process is going on loop it */
	while(ros::ok()){
		/*subscribe to odom */ 
		this->odomSub();
		/* Subscribe to point cloud */ 
		this->cloudSub();
		/* Calculate distance */
		float distance;
		std::vector<double> now_odom;
		curr_odom.push_back(pose_x);
		curr_odom.push_back(pose_y);
		curr_odom.push_back(yaw);
		boost::tie(vertex_It,vertex_End) = boost::vertices(gr);
		std::vector<double> prev_odom;
		prev_odom = gr[*vertex_End].data;
		distance = this->calculateDist(now_odom, prev_odom);
		
		/* if distance is greater than threshold */
		if(distance >= threshold_distance){
			/*  add vertex to graph  */
			this->addVertex(count,now_odom);
			/* calculate transform */
			tr_mat =this->estTrans(curr_pc,prev_cld);
			/* Add edge weights between current and previous */
			this->addEdge(tr_mat); 
			prev_cld = curr_pc;
			count++;
		}
		
		ros::spinOnce();
		}
		boost::write_graphviz(cout, gr);
	}


int main(int argc, char **argv)
{
	/*
	 * Initialize Ros components. Subscribe to point cloud , odometry.
	 */ 
	 
	ros::init(argc,argv,"graph_node");

	Node graphNode = Node();	
	/* Write data to file */

	
	return 0;
}

