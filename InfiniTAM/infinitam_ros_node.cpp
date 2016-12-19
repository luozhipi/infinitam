// Copyright 2014-2015 Isis Innovation Limited and the authors of InfiniTAM

#include <glog/logging.h>
#include <cstdlib>
#include <string>

#include <pcl/io/obj_io.h>
#include "Engine/CLIEngine.h"
#include "Engine/ImageSourceEngine.h"
#include "Engine/Kinect2Engine.h"
#include "Engine/LibUVCEngine.h"
#include "Engine/OpenNIEngine.h"
#include "Engine/PoseSourceEngine.h"
#include "Engine/RealSenseEngine.h"
#include "Engine/RosImageSourceEngine.h"
#include "Engine/RosPoseSourceEngine.h"
#include "Engine/UIEngine.h"

//  ROS
#include <geometric_shapes/shapes.h>
#include <pcl/ros/conversions.h>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud2_iterator.h>
#include <shape_msgs/Mesh.h>
#include <std_srvs/Empty.h>
#include <std_srvs/SetBool.h>

//  TEST
#include <pcl/PolygonMesh.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl_msgs/PolygonMesh.h>
#include <shape_msgs/Mesh.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/Marker.h>

using namespace InfiniTAM::Engine;

/** Create a default source of depth images from a list of command line
 arguments. Typically, @para arg1 would identify the calibration file to
 use, @para arg2 the colour images, @para arg3 the depth images and
 @para arg4 the IMU images. If images are omitted, some live sources will
 be tried.
 */
class InfinitamNode {
 public:
  InfinitamNode(int& argc, char** argv);
  ~InfinitamNode();

  //! Read parameters from the ROS parameter server.
  void readParameters();

  //! Choose Image and Pose sources.
  void SetUpSources();

  //! ROS Service Callback method, initialises Infinitam
  bool startInfinitam(std_srvs::SetBool::Request& request,
                      std_srvs::SetBool::Response& response);

  //! ROS Service Callback method, make infinitam publish the current map.
  bool publishMap(std_srvs::Empty::Request& request,
                  std_srvs::Empty::Response& response);

  //! Converts the internal Mesh to a PCL point cloud.
  void extractITMMeshToPclCloud(
      const ITMMesh::Triangle* triangleArray,
      pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud_pcl);

  //! Converts the internal Mesh to a PCL PolygonMesh.
  void extractITMMeshToPolygonMesh(const ITMMesh::Triangle* triangleArray,
                                   pcl::PolygonMesh::Ptr polygon_mesh_ptr);

  //! Converts the internal Mesh to a ROS Mesh.
  void extractITMMeshToRosMesh(const ITMMesh::Triangle* triangleArray,
                               shape_msgs::Mesh::Ptr ros_mesh);

  //! Converts a PCL PolygonMesh to a ROS Mesh.
  bool convertPolygonMeshToRosMesh(const pcl::PolygonMesh::Ptr in,
                                   shape_msgs::Mesh::Ptr mesh);

 private:
  ros::NodeHandle node_handle_;

  const char* arg1 = "";
  const char* arg2 = NULL;
  const char* arg3 = NULL;
  const char* arg4 = NULL;
  int argc;
  char** argv;

  ITMMainEngine* main_engine_ = nullptr;
  ITMLibSettings* internal_settings_ = nullptr;
  ImageSourceEngine* image_source_ = nullptr;
  IMUSourceEngine* imu_source_ = nullptr;
  PoseSourceEngine* pose_source_ = nullptr;

  ros::Subscriber rgb_sub_;
  ros::Subscriber depth_sub_;
  ros::Subscriber tf_sub_;
  std::string rgb_image_topic;
  std::string depth_image_topic;

  //! Name for the depth camera frame id in TF.
  std::string camera_frame_id_;

  // initialize service
  ros::ServiceServer start_infinitam_service_;

  ros::ServiceServer build_mesh_service_;

  ros::ServiceServer publish_mesh_service_;

  //! ROS publisher to send out the complete cloud.
  ros::Publisher complete_point_cloud_pub_;
  //! ROS topic name where the generated complete cloud is published.
  std::string complete_cloud_topic_;

  //! ROS publisher to send out the complete cloud as ROS mesh.
  ros::Publisher complete_mesh_pub_;
  //! ROS topic name where the generated complete mesh is published.
  std::string complete_mesh_topic_;

  //! ROS Mesh of the map.
  shape_msgs::Mesh::Ptr ros_scene_mesh_ptr_;

  bool save_cloud_to_file_system_;

  bool publish_point_cloud_;

  bool publish_mesh_;

  //! PCL Mesh of the map
  pcl::PolygonMesh::Ptr mesh_ptr_;
};

InfinitamNode::InfinitamNode(int& argc, char** argv) : node_handle_("~") {
  this->argc = argc;
  this->argv = argv;

  pose_source_ = new PoseSourceEngine();
  internal_settings_ = new ITMLibSettings();
  mesh_ptr_.reset(new pcl::PolygonMesh);

  readParameters();

  // Initialize service.
  start_infinitam_service_ = node_handle_.advertiseService(
      "start_infinitam", &InfinitamNode::startInfinitam, this);

  publish_mesh_service_ = node_handle_.advertiseService(
      "publish_mesh", &InfinitamNode::publishMap, this);

  // ROS publishers.
  complete_point_cloud_pub_ = node_handle_.advertise<sensor_msgs::PointCloud2>(
      complete_cloud_topic_, 5);

  complete_mesh_pub_ =
      node_handle_.advertise<shape_msgs::Mesh>(complete_mesh_topic_, 5);
}

InfinitamNode::~InfinitamNode() {
  delete main_engine_;
  delete internal_settings_;
  delete image_source_;
  if (imu_source_ != NULL) delete imu_source_;
}

bool InfinitamNode::startInfinitam(std_srvs::SetBool::Request& request,
                                   std_srvs::SetBool::Response& response) {
  LOG(INFO) << "startInfinitam start!";

  // turn on infinitam
  if (request.data) {
    int arg = 1;
    do {
      if (argv[arg] != NULL)
        arg1 = argv[arg];
      else
        break;
      ++arg;
      if (argv[arg] != NULL)
        arg2 = argv[arg];
      else
        break;
      ++arg;
      if (argv[arg] != NULL)
        arg3 = argv[arg];
      else
        break;
      ++arg;
      if (argv[arg] != NULL)
        arg4 = argv[arg];
      else
        break;
    } while (false);
    printf("after while\n");

    if (arg == 1) {
      printf(
          "usage: %s [<calibfile> [<imagesource>] ]\n"
          "  <calibfile>   : path to a file containing intrinsic calibration "
          "parameters\n"
          "  <imagesource> : either one argument to specify OpenNI device ID\n"
          "                  or two arguments specifying rgb and depth file "
          "masks\n"
          "\n"
          "examples:\n"
          "  %s ./Files/Teddy/calib.txt ./Files/Teddy/Frames/%%04i.ppm "
          "./Files/Teddy/Frames/%%04i.pgm\n"
          "  %s ./Files/Teddy/calib.txt\n\n",
          argv[0], argv[0], argv[0]);
    }

    LOG(INFO) << "initialising ...";

    SetUpSources();

    main_engine_ = new ITMMainEngine(internal_settings_, &image_source_->calib,
                                     image_source_->getRGBImageSize(),
                                     image_source_->getDepthImageSize());

    if (image_source_ == NULL) {
      std::cout << "failed to open any image stream" << std::endl;
    }

    image_source_->main_engine_ = main_engine_;
    pose_source_->main_engine_ = main_engine_;

    UIEngine::Instance()->Initialise(argc, argv, image_source_, imu_source_,
                                     main_engine_, "./Files/Out",
                                     internal_settings_->deviceType);

    // Start already with processing once the run method is called.
    UIEngine::Instance()->mainLoopAction = UIEngine::PROCESS_VIDEO;
    ROS_INFO("GUI Engine Initialized.");
    UIEngine::Instance()->Run();
    ROS_INFO("Done.");
    image_source_->set_camera_pose_ = false;
    UIEngine::Instance()->Shutdown();
  }

  // turn off infinitam.
  if (!request.data) {
    UIEngine::Instance()->mainLoopAction = UIEngine::PROCESS_PAUSED;
    UIEngine::Instance()->mainLoopAction = UIEngine::EXIT;
  }
  // TODO(gocarlos): when the service is called, it does not return true until
  // infinitam is stopped.
  // find a solution.
  return true;
}

bool InfinitamNode::publishMap(std_srvs::Empty::Request& request,
                               std_srvs::Empty::Response& response) {
  ROS_INFO_STREAM("publishMap start.");
  // Make the mesh ready for reading.
  main_engine_->GetMeshingEngine()->MeshScene(main_engine_->GetMesh(),
                                              main_engine_->GetScene());

  // Get triangles from the device's memory.
  ORUtils::MemoryBlock<ITMMesh::Triangle>* cpu_triangles;
  bool rm_triangle_from_cuda_memory = false;
  if (main_engine_->GetMesh()->memoryType == MEMORYDEVICE_CUDA) {
    cpu_triangles = new ORUtils::MemoryBlock<ITMMesh::Triangle>(
        main_engine_->GetMesh()->noMaxTriangles, MEMORYDEVICE_CPU);
    cpu_triangles->SetFrom(
        main_engine_->GetMesh()->triangles,
        ORUtils::MemoryBlock<ITMMesh::Triangle>::CUDA_TO_CPU);
    rm_triangle_from_cuda_memory = true;
  } else {
    cpu_triangles = main_engine_->GetMesh()->triangles;
  }

  // Read the memory and store it in a new array.
  ITMMesh::Triangle* triangleArray = cpu_triangles->GetData(MEMORYDEVICE_CPU);

  ROS_ERROR_COND(main_engine_->GetMesh()->noTotalTriangles < 1,
                 "The mesh has too few triangles, only: %d",
                 main_engine_->GetMesh()->noTotalTriangles);

  //  if (save_cloud_to_file_system_) {
  if (true) {
    // write a STL or OBJ File to the file system.
    std::string filename =
        "../output_" + std::to_string(ros::Time::now().toSec()) + ".stl";
    main_engine_->GetMesh()->WriteSTL(filename.c_str());
    std::string filename2 =
        "../output_" + std::to_string(ros::Time::now().toSec()) + ".obj";
    main_engine_->GetMesh()->WriteOBJ(filename2.c_str());
  }

  if (publish_point_cloud_) {
    // Publish point cloud.
    pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud_pcl(
        new pcl::PointCloud<pcl::PointXYZ>);

    extractITMMeshToPclCloud(triangleArray, point_cloud_pcl);
    LOG(INFO) << "got point cloud";

    sensor_msgs::PointCloud2 point_cloud_msg;
    pcl::toROSMsg(*point_cloud_pcl, point_cloud_msg);
    point_cloud_msg.header.frame_id = camera_frame_id_;
    point_cloud_msg.header.stamp = ros::Time::now();

    complete_point_cloud_pub_.publish(point_cloud_msg);

    // // Publish ROS mesh
    // shape_msgs::Mesh::Ptr ros_mesh(new shape_msgs::Mesh);
    // extractMeshToRosMesh(ros_mesh);
    // ROS_INFO("got ros mesh");
    // sensor_msgs::PointCloud2 point_cloud_msg;
    // pcl::toROSMsg(*point_cloud_pcl, point_cloud_msg);
    // point_cloud_msg.header.frame_id = camera_frame_id_;
    // point_cloud_msg.header.stamp = ros::Time::now();
    // complete_point_cloud_pub_.publish(point_cloud_msg);

    // complete_mesh_pub_.publish(ros_mesh);
  }

  if (publish_mesh_) {
    ROS_INFO_STREAM("publish_mesh_");

    // Get the Mesh as PCL PolygonMesh .
    extractITMMeshToPolygonMesh(triangleArray, mesh_ptr_);

    ROS_INFO_STREAM("Loaded a PolygonMesh with "
                    << mesh_ptr_->cloud.width * mesh_ptr_->cloud.height
                    << " points and " << mesh_ptr_->polygons.size()
                    << " polygons.");

    ROS_INFO_STREAM("mesh_ptr_->cloud.data[12]:" << mesh_ptr_->cloud.data[12]
                                                 << "|");
    std::cout << "mesh_ptr_->polygons[8].vertices:" << mesh_ptr_->polygons[8]
              << "|";

    pcl::io::saveOBJFile("../blabla.obj", *mesh_ptr_);

    // Convert PCL PolygonMesh into ROS shape_msgs Mesh.
    //    convertPolygonMeshToRosMesh(mesh_ptr_, ros_scene_mesh_ptr_);
    //    ROS_INFO_STREAM("Got ROS Mesh.");

    // Publish ROS Mesh.
    //    complete_mesh_pub_.publish(ros_scene_mesh_ptr_);
    //    ROS_INFO_STREAM("ROS Mesh published.");
  }

  if (rm_triangle_from_cuda_memory) {
    delete cpu_triangles;
  }
  ROS_INFO_STREAM("publishMap end!");
  return true;
}

void InfinitamNode::extractITMMeshToPolygonMesh(
    const ITMMesh::Triangle* triangleArray,
    pcl::PolygonMesh::Ptr polygon_mesh_ptr) {
  if (triangleArray == NULL) {
    ROS_ERROR_STREAM("triangleArray == NULL");
    return;
  }

  std::size_t nr_triangles = 0;
  std::size_t nr_points = 0;
  nr_triangles = main_engine_->GetMesh()->noTotalTriangles;
  nr_points = nr_triangles * 3;
  ROS_INFO_STREAM("nr_triangles:  " << nr_triangles);
  ROS_INFO_STREAM("nr_points:  " << nr_points);

  pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud_pcl(
      new pcl::PointCloud<pcl::PointXYZ>);

  point_cloud_pcl->width = nr_points;
  point_cloud_pcl->height = 1;
  point_cloud_pcl->is_dense = true;
  point_cloud_pcl->points.resize(point_cloud_pcl->width *
                                 point_cloud_pcl->height);

  std::size_t point_number = 0;

  // All vertices of the mesh are stored in the pcl point cloud.
  for (std::size_t i = 0; i < nr_triangles; ++i) {
    point_cloud_pcl->points[point_number].x = triangleArray[i].p0.x;
    point_cloud_pcl->points[point_number].y = triangleArray[i].p0.y;
    point_cloud_pcl->points[point_number].z = triangleArray[i].p0.z;
    ++point_number;
    point_cloud_pcl->points[point_number].x = triangleArray[i].p1.x;
    point_cloud_pcl->points[point_number].y = triangleArray[i].p1.y;
    point_cloud_pcl->points[point_number].z = triangleArray[i].p1.z;
    ++point_number;
    point_cloud_pcl->points[point_number].x = triangleArray[i].p2.x;
    point_cloud_pcl->points[point_number].y = triangleArray[i].p2.y;
    point_cloud_pcl->points[point_number].z = triangleArray[i].p2.z;
    ++point_number;
  }

  // Build the point cloud.
  pcl::toROSMsg(*point_cloud_pcl, mesh_ptr_->cloud);

  //  cloud_ptr->width = cloud_ptr->height = cloud_ptr->point_step =
  //      cloud_ptr->row_step = 0;
  //  cloud_ptr->data.clear();
  //
  //  int field_offset = 0;
  //  for (int i = 0; i < 3; ++i, field_offset += 4) {
  //    cloud_ptr->fields.push_back(pcl::PCLPointField());
  //    cloud_ptr->fields[i].offset = field_offset;
  //    cloud_ptr->fields[i].datatype = pcl::PCLPointField::FLOAT32;
  //    cloud_ptr->fields[i].count = 1;
  //  }
  //
  //  cloud_ptr->fields[0].name = "x";
  //  cloud_ptr->fields[1].name = "y";
  //  cloud_ptr->fields[2].name = "z";
  //
  //  cloud_ptr->point_step = field_offset;
  //  cloud_ptr->width = nr_points;
  //  cloud_ptr->height = 1;
  //  cloud_ptr->row_step = cloud_ptr->point_step * cloud_ptr->width;
  //  cloud_ptr->is_dense = true;
  //  cloud_ptr->data.resize(cloud_ptr->point_step * nr_points);
  //
  //  ROS_INFO_STREAM("cloud_ptr->data.size: " << cloud_ptr->data.size());
  //  ROS_INFO_STREAM("cloud_ptr->point_step: " << cloud_ptr->point_step);
  //  ROS_INFO_STREAM("cloud created!");

  // write vertices
  std::size_t v_idx = 0;

  ROS_INFO_STREAM("going to fill the mesh with points.");

  //  for (uint i = 0; i < nr_triangles; ++i) {
  //    for (size_t f = 0; f < 3; ++f) {
  //      polygon_mesh_ptr->cloud.data[v_idx * mesh_ptr_->cloud.point_step +
  //                            mesh_ptr_->cloud.fields[f].offset] =
  //          triangleArray[i].p0[f];
  //
  //    }  // Write first point for one triangle.
  //    ++v_idx;
  //
  //    for (size_t f = 0; f < 3; ++f) {
  //      polygon_mesh_ptr->cloud.data[v_idx * mesh_ptr_->cloud.point_step +
  //                            mesh_ptr_->cloud.fields[f].offset] =
  //          triangleArray[i].p1[f];
  //
  //    }  // Write second point for one triangle.
  //    ++v_idx;
  //
  //    for (size_t f = 0; f < 3; ++f) {
  //      polygon_mesh_ptr->cloud.data[v_idx * mesh_ptr_->cloud.point_step +
  //                            mesh_ptr_->cloud.fields[f].offset] =
  //          triangleArray[i].p2[f];
  //
  //    }  // Write third point for one triangle.
  //    ++v_idx;
  //
  //  }  // Write 3 points for one triangle.

  mesh_ptr_->polygons.resize(nr_triangles);

  for (uint i = 0; i < nr_triangles; ++i) {
    //  Write faces.
    mesh_ptr_->polygons[i].vertices.resize(3);
    // The vertex index starts with 1 not with 0 (OBJ-file standard).
    // the Obj_io.h defines that the vertices start with 0, when writing a file,
    // it just adds one to each value.
    // polygon_mesh_ptr->polygons[i].vertices.push_back(i * 3 + 2 + 1);
    // polygon_mesh_ptr->polygons[i].vertices.push_back(i * 3 + 1 + 1);
    // polygon_mesh_ptr->polygons[i].vertices.push_back(i * 3 + 0 + 1);
    polygon_mesh_ptr->polygons[i].vertices[0] = (i * 3 + 2);
    polygon_mesh_ptr->polygons[i].vertices[1] = (i * 3 + 1);
    polygon_mesh_ptr->polygons[i].vertices[2] = (i * 3 + 0);
  }

  ROS_INFO_STREAM("cloud filled: header: "
                  << mesh_ptr_->cloud.header
                  << "height: " << mesh_ptr_->cloud.height
                  << " width: " << mesh_ptr_->cloud.width
                  << " fields.size: " << mesh_ptr_->cloud.fields.size());

  ROS_INFO_STREAM("Polygons vector size: " << mesh_ptr_->polygons.size());
}

void InfinitamNode::extractITMMeshToPclCloud(
    const ITMMesh::Triangle* triangleArray,
    pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud_pcl) {
  //  LOG(INFO) << "extractITMMeshToPclCloud start. ";
  //  CHECK_NOTNULL(main_engine_);
  //  CHECK_NOTNULL(&point_cloud_pcl);
  //
  //  // Point cloud has at least 3 points per triangle.
  //  point_cloud_pcl->width = main_engine_->GetMesh()->noTotalTriangles * 3;
  //  point_cloud_pcl->height = 1;
  //  point_cloud_pcl->is_dense = false;
  //  point_cloud_pcl->points.resize(point_cloud_pcl->width *
  //                                 point_cloud_pcl->height);
  //
  //  ROS_ERROR_COND(main_engine_->GetMesh()->noTotalTriangles < 1,
  //                 "The mesh has too few triangles, only: %d",
  //                 main_engine_->GetMesh()->noTotalTriangles);
  //
  //  std::size_t point_number = 0;
  //
  //  // All vertices of the mesh are stored in the pcl point cloud.
  //  for (int64 i = 0; i < main_engine_->GetMesh()->noTotalTriangles * 3;
  //       i = i + 3) {
  //    point_cloud_pcl->points[i].x = triangleArray[i].p0.x;
  //    point_cloud_pcl->points[i].y = triangleArray[i].p0.y;
  //    point_cloud_pcl->points[i].z = triangleArray[i].p0.z;
  //
  //    point_cloud_pcl->points[i + 1].x = triangleArray[i].p1.x;
  //    point_cloud_pcl->points[i + 1].y = triangleArray[i].p1.y;
  //    point_cloud_pcl->points[i + 1].z = triangleArray[i].p1.z;
  //
  //    point_cloud_pcl->points[i + 2].x = triangleArray[i].p2.x;
  //    point_cloud_pcl->points[i + 2].y = triangleArray[i].p2.y;
  //    point_cloud_pcl->points[i + 2].z = triangleArray[i].p2.z;
  //  }
  //  LOG(INFO) << "extractITMMeshToPclCloud end.";
}

void InfinitamNode::extractITMMeshToRosMesh(
    const ITMMesh::Triangle* triangleArray, shape_msgs::Mesh::Ptr ros_mesh) {
  //  CHECK_NOTNULL(main_engine_);
  //  CHECK_NOTNULL(&ros_mesh);
  //  LOG(INFO) << "extractMeshToRosMesh start.";
  //
  //  shape_msgs::MeshTriangle ros_triangle;
  //  geometry_msgs::Point vertices;
  //
  //  std::size_t index = 0;
  //  // All vertices of the infinitam mesh are stored in a ROS Mesh.
  //  for (std::size_t i = 0; i < main_engine_->GetMesh()->noTotalTriangles;
  //  ++i) {
  //    vertices.x = triangleArray[i].p0.x;
  //    vertices.y = triangleArray[i].p0.y;
  //    vertices.z = triangleArray[i].p0.z;
  //    ros_mesh->vertices.push_back(vertices);
  //    ros_triangle.vertex_indices[0] = index++;
  //
  //    vertices.x = triangleArray[i].p1.x;
  //    vertices.y = triangleArray[i].p1.y;
  //    vertices.z = triangleArray[i].p1.z;
  //    ros_mesh->vertices.push_back(vertices);
  //    ros_triangle.vertex_indices[1] = index++;
  //
  //    vertices.x = triangleArray[i].p2.x;
  //    vertices.y = triangleArray[i].p2.y;
  //    vertices.z = triangleArray[i].p2.z;
  //    ros_mesh->vertices.push_back(vertices);
  //    ros_triangle.vertex_indices[2] = index++;
  //
  //    ros_mesh->triangles.push_back(ros_triangle);
  //  }
  //  ROS_INFO_STREAM("ROS mesh has "
  //                  << ros_mesh->triangles.size() << " triangles, "
  //                  << "and " << ros_mesh->vertices.size() << " vertices.");
  //
  //  LOG(INFO) << "extractMeshToRosMesh end.";
}

bool InfinitamNode::convertPolygonMeshToRosMesh(
    const pcl::PolygonMesh::Ptr polygon_mesh_ptr,
    shape_msgs::Mesh::Ptr ros_mesh_ptr) {
  pcl_msgs::PolygonMesh pcl_msg_mesh;

  pcl_conversions::fromPCL(*polygon_mesh_ptr, pcl_msg_mesh);

  sensor_msgs::PointCloud2Modifier pcd_modifier(pcl_msg_mesh.cloud);

  size_t size = pcd_modifier.size();

  ros_mesh_ptr->vertices.resize(size);

  ROS_INFO_STREAM("polys: " << pcl_msg_mesh.polygons.size()
                            << " vertices: " << pcd_modifier.size());

  sensor_msgs::PointCloud2ConstIterator<float> pt_iter(pcl_msg_mesh.cloud, "x");

  for (size_t i = 0; i < size; i++, ++pt_iter) {
    ros_mesh_ptr->vertices[i].x = pt_iter[0];
    ros_mesh_ptr->vertices[i].y = pt_iter[1];
    ros_mesh_ptr->vertices[i].z = pt_iter[2];
  }

  ROS_INFO_STREAM("Updated vertices");

  ros_mesh_ptr->triangles.resize(polygon_mesh_ptr->polygons.size());

  for (size_t i = 0; i < polygon_mesh_ptr->polygons.size(); ++i) {
    if (polygon_mesh_ptr->polygons[i].vertices.size() < 3) {
      ROS_WARN_STREAM("Not enough points in polygon. Ignoring it.");
      continue;
    }

    for (int j = 0; j < 3; ++j) {
      ros_mesh_ptr->triangles[i].vertex_indices[j] =
          polygon_mesh_ptr->polygons[i].vertices[j];
    }
  }
  ROS_WARN_STREAM("convertPolygonMeshToRosMesh end");
  return true;
}

void InfinitamNode::readParameters() {
  // Set ROS topic names.
  node_handle_.param<std::string>("rgb_image_topic", rgb_image_topic,
                                  "/camera/rgb/image_raw");
  node_handle_.param<std::string>("depth_image_topic", depth_image_topic,
                                  "/camera/depth/image_raw");
  node_handle_.param<std::string>("scene_point_cloud", complete_cloud_topic_,
                                  "/scene_point_cloud");
  node_handle_.param<std::string>("scene_mesh", complete_mesh_topic_,
                                  "/scene_mesh");

  // Set the output one wants from Infinitam.
  node_handle_.param<bool>("save_cloud_to_file_system",
                           save_cloud_to_file_system_, true);
  node_handle_.param<bool>("publish_point_cloud", publish_point_cloud_, false);
  node_handle_.param<bool>("publish_mesh", publish_mesh_, false);

  // Set InfiniTAM settings.
  node_handle_.param<float>("viewFrustum_min",
                            internal_settings_->sceneParams.viewFrustum_min,
                            0.35f);
  node_handle_.param<float>(
      "viewFrustum_max", internal_settings_->sceneParams.viewFrustum_max, 3.0f);

  node_handle_.param<std::string>("camera_frame_id", camera_frame_id_,
                                  "sr300_depth_optical_frame");
}

void InfinitamNode::SetUpSources() {
  CHECK_NOTNULL(pose_source_);
  CHECK_NOTNULL(internal_settings_);

  const char* calibration_filename = arg1;
  const char* depth_image_filename = arg2;
  const char* rgb_image_filename = arg3;
  const char* filename_imu = arg4;

  printf("using calibration file: %s\n", calibration_filename);

  if (rgb_image_filename != NULL) {
    printf("using rgb images: %s\nusing depth images: %s\n",
           depth_image_filename, rgb_image_filename);
    if (filename_imu == NULL) {
      image_source_ = new ImageFileReader(
          calibration_filename, depth_image_filename, rgb_image_filename);
    } else {
      printf("using imu data: %s\n", filename_imu);
      image_source_ =
          new RawFileReader(calibration_filename, depth_image_filename,
                            rgb_image_filename, Vector2i(320, 240), 0.5f);
      imu_source_ = new IMUSourceEngine(filename_imu);
    }
  }

  if (image_source_ == NULL) {
    printf("trying OpenNI device: %s\n", (depth_image_filename == NULL)
                                             ? "<OpenNI default device>"
                                             : depth_image_filename);
    image_source_ =
        new OpenNIEngine(calibration_filename, depth_image_filename);
    if (image_source_->getDepthImageSize().x == 0) {
      delete image_source_;
      image_source_ = NULL;
    }
  }
  if (image_source_ == NULL) {
    printf("trying UVC device\n");
    image_source_ = new LibUVCEngine(calibration_filename);
    if (image_source_->getDepthImageSize().x == 0) {
      delete image_source_;
      image_source_ = NULL;
    }
  }

  if (image_source_ == NULL) {
    printf("trying MS Kinect 2 device\n");
    image_source_ = new Kinect2Engine(calibration_filename);
    if (image_source_->getDepthImageSize().x == 0) {
      delete image_source_;
      image_source_ = NULL;
    }
  }
  if (image_source_ == NULL) {
    printf("Checking if there are suitable ROS messages being published.\n");

    pose_source_ = new RosPoseSourceEngine(node_handle_);
    image_source_ =
        new RosImageSourceEngine(node_handle_, calibration_filename);

    // Get images from ROS topic.
    rgb_sub_ = node_handle_.subscribe(rgb_image_topic, 10,
                                      &RosImageSourceEngine::rgbCallback,
                                      (RosImageSourceEngine*)image_source_);

    depth_sub_ = node_handle_.subscribe(depth_image_topic, 10,
                                        &RosImageSourceEngine::depthCallback,
                                        (RosImageSourceEngine*)image_source_);

    tf_sub_ =
        node_handle_.subscribe("/tf", 10, &RosPoseSourceEngine::TFCallback,
                               (RosPoseSourceEngine*)pose_source_);

    if (image_source_->getDepthImageSize().x == 0) {
      delete image_source_;
      image_source_ = NULL;
    }
  }

  // this is a hack to ensure backwards compatibility in certain configurations
  if (image_source_ == NULL) {
    return;
  }
  if (image_source_->calib.disparityCalib.params == Vector2f(0.0f, 0.0f)) {
    image_source_->calib.disparityCalib.type = ITMDisparityCalib::TRAFO_AFFINE;
    image_source_->calib.disparityCalib.params = Vector2f(1.0f / 1000.0f, 0.0f);
  }
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  ros::init(argc, argv, "infinitamNode");
  InfinitamNode infinitamNode(argc, argv);

  while (ros::ok()) {
    ros::spin();
  }

  return EXIT_SUCCESS;
}
