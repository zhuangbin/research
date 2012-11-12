// Difference of Normals Segmentation
// Call this function on KITTI pcds with
// parameters: (0.02 0.26 0.0001 15) clusters the ground
#include <string>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/search/organized.h>
#include <pcl/search/kdtree.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/filters/conditional_removal.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/features/don.h>
using namespace pcl;
using namespace std;
int main (int argc, char *argv[])
{
    cout << argc << endl;
    double scale1, scale2, threshold, segradius;
    if (argc < 7)
    {
        cerr << "usage: " << argv[0] << " inputfile smallscale largescale threshold segradius" << endl;
        exit (EXIT_FAILURE);
    }

    /// the file to read from.
    string infile = argv[1];
    istringstream (argv[2]) >> scale1;
    istringstream (argv[3]) >> scale2;
    istringstream (argv[4]) >> threshold;   // threshold for DoN magnitude
    istringstream (argv[5]) >> segradius;   // threshold for radius segmentation
    string outDir = argv[6];

    // load point cloud
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::io::loadPCDFile (infile, *cloud);
    cout << "Total number of points: " << cloud->size() << endl;
    // Create a search tree, use KDTreee for non-organized data.
    pcl::search::Search<PointXYZ>::Ptr tree;
    if (cloud->isOrganized ()){
        tree.reset (new pcl::search::OrganizedNeighbor<PointXYZ> ());
    }
    else{
        tree.reset (new pcl::search::KdTree<PointXYZ> (false));
    }
    // Set the input pointcloud for the search tree
    tree->setInputCloud (cloud);

    if (scale1 >= scale2)
    {
        cerr << "Error: Large scale must be > small scale!" << endl;
        exit (EXIT_FAILURE);
    }
    cout << infile << endl;
    cout << scale1 << " , " << scale2 << " , " << threshold << " , " << segradius << endl;
    // Compute normals using both small and large scales at each point
    pcl::NormalEstimationOMP<PointXYZ, PointNormal> ne;
    ne.setInputCloud (cloud);
    ne.setSearchMethod (tree);
    ne.setViewPoint (std::numeric_limits<float>::max (), std::numeric_limits<float>::max (), std::numeric_limits<float>::max ());
    // calculate normals with the small scale
    cout << "Calculating normals for scale..." << scale1 << endl;
    pcl::PointCloud<PointNormal>::Ptr normals_small_scale (new pcl::PointCloud<PointNormal>);
    ne.setRadiusSearch (scale1);
    ne.compute (*normals_small_scale);
    cout << "computed " << normals_small_scale->size() << endl;
    // calculate normals with the large scale
    cout << "Calculating normals for scale..." << scale2 << endl;
    pcl::PointCloud<PointNormal>::Ptr normals_large_scale (new pcl::PointCloud<PointNormal>);
    ne.setRadiusSearch (scale2);
    ne.compute (*normals_large_scale);
    cout << "computed " << normals_large_scale->size() << endl;
  
    // Create output cloud for DoN results
    PointCloud<PointNormal>::Ptr doncloud (new pcl::PointCloud<PointNormal>);
    copyPointCloud<PointXYZ, PointNormal>(*cloud, *doncloud);
    cout << "Calculating DoN... " << endl;
    // Create DoN operator
    pcl::DifferenceOfNormalsEstimation<PointXYZ, PointNormal, PointNormal> don;
    don.setInputCloud (cloud);
    don.setNormalScaleLarge (normals_large_scale);
    don.setNormalScaleSmall (normals_small_scale);
    if (!don.initCompute ()){
        std::cerr << "Error: Could not intialize DoN feature operator" << std::endl;
        exit (EXIT_FAILURE);
    }
    // Compute DoN
    don.computeFeature(*doncloud);
    // Save DoN features
    pcl::PCDWriter writer;
    // writer.write<pcl::PointNormal> ("don.pcd", *doncloud, false); 
    // Filter by magnitude
    cout << "Filtering out DoN mag <= " << threshold << "..." << endl;
    // Build the condition for filtering
    pcl::ConditionOr<PointNormal>::Ptr range_cond (new pcl::ConditionOr<PointNormal> ());
    range_cond->addComparison (pcl::FieldComparison<PointNormal>::ConstPtr (
          new pcl::FieldComparison<PointNormal> ("curvature", pcl::ComparisonOps::GT, threshold)));
    // Build the filter
    pcl::ConditionalRemoval<PointNormal> condrem (range_cond);
    condrem.setInputCloud(doncloud);
    pcl::PointCloud<PointNormal>::Ptr doncloud_filtered (new pcl::PointCloud<PointNormal>);
    // Apply filter
    condrem.filter (*doncloud_filtered);
    doncloud = doncloud_filtered;
    // Save filtered output
    cout << "Filtered Pointcloud: " << doncloud->points.size() << " data points." << endl;
    string combined = outDir + "filtered.pcd";
    writer.write<pcl::PointNormal> (combined, *doncloud, false); 
    // Filter by magnitude
    cout << "Clustering using EuclideanClusterExtraction with tolerance <= " << segradius << "..." << endl;
    pcl::search::KdTree<PointNormal>::Ptr segtree (new pcl::search::KdTree<PointNormal>);
    segtree->setInputCloud (doncloud);
    
    std::vector<pcl::PointIndices> cluster_indices;
    pcl::EuclideanClusterExtraction<PointNormal> ec;
    ec.setClusterTolerance (segradius);
    ec.setMinClusterSize (50);
    ec.setMaxClusterSize (100000);
    ec.setSearchMethod (segtree);
    ec.setInputCloud (doncloud);
    ec.extract (cluster_indices);

    int j = 0;
    for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it, j++)
    {
        pcl::PointCloud<PointNormal>::Ptr cloud_cluster_don (new pcl::PointCloud<PointNormal>);
        for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); pit++)
        {
            cloud_cluster_don->points.push_back (doncloud->points[*pit]);
        }
        cloud_cluster_don->width = int (cloud_cluster_don->points.size ());
        cloud_cluster_don->height = 1;
        cloud_cluster_don->is_dense = true;
        //Save cluster
        cout << "PointCloud representing the Cluster: " << cloud_cluster_don->points.size () << " data points." << std::endl;
        stringstream ss;
        ss << outDir << "/cluster/" << "cluster_" << j << ".pcd";
        writer.write<pcl::PointNormal> (ss.str (), *cloud_cluster_don, false);
    }
}