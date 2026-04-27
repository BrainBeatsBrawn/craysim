module;

#include <string>
#include <iostream>
#include <cstdint>
#include <vector>
#include <array>
#include <memory>

#include <MulticamScene.h>
#include <libEyeRenderer.h> // getCurrentEyeSamplesPerOmmatidium

// scene exists at global scope in libEyeRenderer.so
extern MulticamScene* scene;

export module craysim.visual;

import sm.mathconst;
import sm.vvec;
import sm.quaternion;
import sm.mat;
import sm.hdfdata;
import sm.algo;
import sm.geometry;
import sm.config;

import mplot.tools;
import mplot.compoundray.interop; // mathplot <--> compoundray interoperability
import mplot.compoundray.ommatidium; // The mplot::Ommatidium structure
import mplot.compoundray.eyevisual;
import mplot.instancedscattervisual;
import mplot.normalsvisual;
import mplot.coordarrows;

export import mplot.gl.version;
export import mplot.visual;
export import mplot.fps.profiler;
export import oces.reader;

import craysim.random_walk;

// Reproduce controller functions for the mplot window for ease of use
export namespace craysim
{
    void print_help (const char* progname)
    {
        std::cout << "USAGE:\n" << progname << " -f <path to gltf scene>\n\n"
                  << "\t-h\tDisplay this help information.\n"
                  << "\t-f\tPath to a gltf scene file (absolute or relative to current "
                  << "working directory, e.g. './data/axis_coloured_blocks.gltf').\n";
    }

    // Flags class
    enum class options : std::uint32_t
    {
        blender_axes,     // Set true to transform glTF into Blender's z-up axes
        max_fps,          // If true, poll, instead of fps
        path_from_csv,    // Move the agent from a pre-defined sequence of 2D coordinates that give it a path
        api_movement,     // Client code sets a vec/quat or mat for movement in the next render_and_poll()
        homing_mode,      // A flag for a 'go home' mode. It's up to client code to decide what to do with this.
        have_film_director, // user passed a json config file for film direction
        save_hdf5,        // If true, then save any output data in HDF5 (active in 'path_from_csv' mode)
        debug_mv,         // Open a debug h5 file (craysim.h5) and run compute_mesh_movement once for debug of NavMesh
        can_exit          // If set, program can exit now
    };

    // craysim::parse_inputs returns this struct
    struct parsed_inputs
    {
        sm::flags<craysim::options> opts;
        std::string gltf_path = {};
        std::string film_director_path = {};
        std::string csv_path = {};
        std::string h5_path = {};
        std::string hovh = {};
    };

    // Parse cmd line to find the path and set options. Return filepath of main scene gltf file and any csv path
    parsed_inputs parse_inputs (std::int32_t argc, char* argv[])
    {
        parsed_inputs rtn;

        for (std::int32_t i = 0; i < argc; i++) {
            std::string arg = std::string(argv[i]);
            if (arg == "-h") {
                craysim::print_help (argv[0]);
                rtn.opts |= craysim::options::can_exit;
            } else if (arg == "-f") {
                rtn.gltf_path = std::string(argv[++i]);
            } else if (arg == "-b") {
                rtn.opts |= craysim::options::blender_axes;
            } else if (arg == "-x") {
                rtn.opts |= craysim::options::max_fps;
            } else if (arg == "-c") {
                rtn.opts |= craysim::options::path_from_csv;
                i++;
                rtn.csv_path = std::string(argv[i]);
            } else if (arg == "-j") {
                rtn.opts |= craysim::options::have_film_director;
                i++;
                rtn.film_director_path = std::string(argv[i]);
            } else if (arg == "-d") {
                rtn.opts |= craysim::options::save_hdf5;
            } else if (arg == "-g") {
                rtn.opts |= craysim::options::debug_mv;
            } else if (arg == "-H") {
                rtn.hovh = std::string(argv[++i]);
            }
        }
        if (rtn.gltf_path.empty()) {
            craysim::print_help (argv[0]);
            rtn.opts |= craysim::options::can_exit;
        }

        rtn.h5_path = rtn.csv_path;
        mplot::tools::stripFileSuffix (rtn.h5_path);
        if (rtn.h5_path.empty()) { rtn.h5_path = "trail"; }
        rtn.h5_path += ".h5";

        return rtn;
    }

    // For a given samples per omm, return a sensible number of loops over which to average fps, so
    // that fps takes around 1 sec to stabilize.
    constexpr std::uint32_t best_n_samples (std::int32_t samples_per_omm)
    {
        std::uint32_t best_n = 0;
        switch (samples_per_omm) {
        case 1:
        case 2:
        {
            best_n = 1024; // about a seconds worth
            break;
        }
        case 4:
        case 8:
        case 16:
        case 32:
        case 64:
        {
            best_n = 512;
            break;
        }
        case 128:
        case 256:
        {
            best_n = 256;
            break;
        }
        case 512:
        {
            best_n = 128;
            break;
        }
        case 1024:
        case 2048:
        {
            best_n = 64;
            break;
        }
        default:
        {
            best_n = 32;
        }
        }
        return best_n;
    }

    // Read a simple csv with 2D coordinates, using first two entries on each line
    bool read_csv (const std::string& path, sm::vvec<sm::vec<float, 2>>& positions)
    {
        std::ifstream f (path.c_str(), std::ios::in);
        if (f.is_open() == false) { return false; }
        std::string line;
        std::vector<std::string> tokens;
        while (std::getline (f, line)) {
            sm::vec<float, 2> twodpos;
            // Tokenize line into the coordinates
            twodpos.set_from_str (line, ",");
            positions.push_back (twodpos);
        }
        return true;
    }

    template <int glver>
    struct visual final : public mplot::Visual<glver>
    {
        using mc = sm::mathconst<float>;

        // When the program starts, how many samples per ommatidium/element do you want?
        static constexpr std::int32_t samples_per_omm_default = 64;

        visual (std::int32_t width, std::int32_t height, const std::string& title, craysim::parsed_inputs& prog_opts)
            : mplot::Visual<glver> (width, height, title)
        {
            this->sim_opts = prog_opts.opts;

            // Boilerplate memory alloc for compound-ray and turn off verbose logging.
            multicamAlloc(); setVerbosity (false);

            this->lightingEffects (true);
            // Use a non-default zFar as we are likely to use large environments
            this->zFar = 2400;
            // Rotate about the nearest VisualModel
            this->rotateAboutNearest (true);
            // Rotate about a scene vertical axis? true for landscapes, false for cubes/objects (Ctrl-k changes I think, at runtime)
            this->rotateAboutVertical (true);
            // A blue sky background colour by default (client code can change this)
            this->bgcolour = { 0.298f, 0.412f,  0.576f };
            // State defaults
            //this->vstate |= state::show_camframe;
            if (this->sim_opts.test(craysim::options::blender_axes)) {
                this->switch_scene_vertical_axis(); // to uz up
                this->updateCoordLabels ("X", "Y", "Z(up)");
            } else {
                this->updateCoordLabels ("X", "Y(up)", "Z");
                // We start rotated into a drone view initial orientation for taking pictures of the world.
                // Into craysim::visual (with the blender_axes==true equivalent)...
                sm::quaternion<float> def_q (sm::vec<float>::ux(), mc::pi_over_2); // non-blender only
                this->setSceneRotation (def_q);
            }

            // We follow the agent as it moves by default.
            this->options.set (mplot::visual_options::viewFollowsVMTranslations);

            this->load (prog_opts.gltf_path);
            // Use a FPS profiling with a text object on screen
            this->addLabel ("0 FPS", {0.63f, -0.43f, 0.0f}, this->fps_label);
            this->setup_camera();
            this->setup_oces();
            this->setup_eyevisual();
            this->setup_breadcrumbs (1000u); // default 1000 breadcrumbs
            this->setup_agent_coords();

            // For json film direction, first try a path based on any csv path we have
            if (prog_opts.film_director_path.empty() && !prog_opts.csv_path.empty()) {
                // Construct json path from csv path and try that
                std::string candidate_json = prog_opts.csv_path;
                mplot::tools::stripFileSuffix (candidate_json);
                candidate_json += ".json";
                std::cout << "Attempt to open csv's partner json: " << candidate_json << std::endl;
                this->setup_film_director (candidate_json);

            } else {
                if (!prog_opts.film_director_path.empty()) {
                    this->setup_film_director (prog_opts.film_director_path);
                }
            }

            this->record.init (prog_opts.h5_path, std::ios::out | std::ios::trunc);
        }

        ~visual()
        {
            stop(); // stop compound-ray from running
            multicamDealloc(); // De-allocate compound-ray memory
        }

        void load (const std::string& gltfpath)
        {
            // Load the file
            this->path = gltfpath;
            this->basepath = this->path;
            std::cout << "Loading glTF file \"" << this->path << "\"..." << std::endl;
            mplot::tools::stripUnixFile (this->basepath);
            std::cout << "glTF dir: " << this->basepath << std::endl;
            loadGlTFscene (this->path.c_str(), (this->sim_opts.test (craysim::options::blender_axes)
                                                ? mplot::compoundray::blender_transform() : sutil::Matrix4x4::identity()));
            // Get the visual models from the scene
            mplot::compoundray::scene_to_visualmodels<glver> (scene, this, false); // true for 'make_navmeshes'
        }

        void setup_camera()
        {
            // We get the eye data path from the glTF file
            std::int32_t ncam = static_cast<std::int32_t>(getCameraCount());
            std::int32_t num_compound_cameras = 0;
            std::int32_t my_compound_camera = -1;
            for (std::int32_t ci = 0; ci < ncam; ++ci) {
                gotoCamera (ci);
                this->efpath = getEyeDataPath();
                if (!this->efpath.empty()) {
                    ++num_compound_cameras;
                    my_compound_camera = ci;
                }
            }
            if (num_compound_cameras > 1) {
                throw std::runtime_error ("This program works for only one compound eye camera in your gltf.");
            }
            // Now switch to our compound ray camera and set the samples per ommatidium/element
            if (my_compound_camera != -1) {
                gotoCamera (my_compound_camera);
                std::int32_t csamp = getCurrentEyeSamplesPerOmmatidium();
                std::cout << "Current eye samples per ommatidium is " << csamp << std::endl;
                if (csamp < 32000) { changeCurrentEyeSamplesPerOmmatidiumBy (samples_per_omm_default - csamp); }
            }
        }

        void setup_oces()
        {
            // Use oces_reader to read in our eye data, esp. for the head
            std::string oces_path = this->efpath;
            mplot::tools::stripFileSuffix (oces_path);
            oces_path += ".gltf";
            // Now try to open oces_path
            std::cout << "Attempt to load OCES file " << oces_path << "\n";
            this->oces_reader.read (oces_path);
            if (oces_reader.read_success == false) {
                std::cout << "No associated OCES file for a head\n";
            } else {
                // Read the head and make a VisualModel
                oces_reader.head_mesh.single_colour = {0.345f, 0.122f, 0.082f};
            }
        }

        // In-scene visualization of our compound-eye
        void setup_eyevisual()
        {
            // We get the initial camera localspace. This also serves to reset the camera pose. This
            // is set in the GLTF file and note that it may be a LEFT HANDED coordinate system! We
            // update this if we have a landscape, and it is updated to the first pose 'on the land'
            sm::mat<float, 4> ics = mplot::compoundray::getCameraSpace (scene);
            this->initial_camera_space.translate (ics.translation()); // Right handed

            // Create an EyeVisual 'eye' in our scene
            auto eyevm = std::make_unique<mplot::compoundray::EyeVisual<glver>> (sm::vec<>{}, &this->ommatidia_data, this->get_ommatidia_ptr(), this->get_head_mesh());
            eyevm->set_parent (this->get_id());
            eyevm->setViewMatrix (this->initial_camera_space);
            eyevm->name = "EyeVisual";
            eyevm->finalize();
            this->eye = this->addVisualModel (eyevm);
            // This eye is the followed VM.
            this->setFollowedVM (this->eye);
        }

        // Breadcrumb trail for max_bc breadcrumbs. Called at start of program, can be re-called
        void setup_breadcrumbs (std::uint64_t max_bc)
        {
            if (this->isvp != nullptr) {
                // check max_bc same as max_instances
                if (max_bc == isvp->max_instances) {
                    // Nothing to do
                    return;
                }
                // Remove existing
                this->removeVisualModel (this->isvp);
                this->isvp = nullptr;
            }
            auto isv = std::make_unique<mplot::InstancedScatterVisual<glver>> (sm::vec<>{});
            isv->set_parent (this->get_id());
            isv->max_instances = max_bc;
            isv->radiusFixed = 0.004f;
            isv->finalize();
            this->isvp = this->addVisualModel (isv);
        }

        void setup_agent_coords()
        {
            // Make CoordArrows axes to show our camera's localspace (and to help find our tiny ant)
            auto antca = std::make_unique<mplot::CoordArrows<glver>> (sm::vec<>{});
            antca->set_parent (this->get_id());
            antca->em = 0.0f; // labels don't work so well
            float len = 2.0f;
            antca->lengths = { len, len, len };
            antca->thickness = 1.0f;
            antca->endsphere_size = 1.2f;
            antca->finalize();
            this->agent_coords = this->addVisualModel (antca);
            this->agent_coords->name = "agent";
            this->agent_coords->setViewMatrix (this->initial_camera_space);
        }

        // Probably to go to mathplot
        void setup_film_director (const std::string& path)
        {
            try {
                this->film_director.init (path);
                if (this->film_director.ready) {
                    // Get list of movement time points at which camera movements should be created
                    nlohmann::json directions = this->film_director.get ("directions");
                    // Iterate though directions
                    for (auto dirn : directions) {
                        sm::config c (dirn);

                        std::uint32_t t = c.get<std::uint32_t>("event_time", 0);
                        std::string et = c.get<std::string>("event_type", "unknown");

                        if (et == "sceneview") {
                            this->directions[t] = mplot::direction_data();
                            this->directions[t].sceneview = c.get_vec<float, 16> ("sceneview");
                            this->directions[t].event = mplot::direction_event::sceneview;
                        } else if (et == "timed_translation") {
                            this->directions[t] = mplot::direction_data();
                            this->directions[t].translation = c.get_vec<float, 3> ("translation");
                            this->directions[t].transform_time = c.get<float> ("transform_time", 1.0f);
                            this->directions[t].event = mplot::direction_event::timed_translation;
                        } else {
                            std::cout << "Unknown event type\n";
                        }
                    }
                } else {
                    std::cout << "Failed to open JSON file " << path << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Failed to open JSON file " << path << " (exception: " << e.what() << ")";
            }
        }

        void setup_random_walk (const std::uint32_t _n_steps = 1500, const std::uint32_t _a_tau = 150, const float _kappa = 100, const float _a_max = 100)
        {
            this->rrg = std::make_unique<craysim::random_walk<float>>(_n_steps, _a_tau, _kappa, _a_max);
        }

        void clear_breadcrumbs()
        {
            this->move_counter = 0;
            this->breadcrumb_coords.clear();
            this->breadcrumb_data.clear();
            // Leave bc_clr/bc_alpha/bc_scale for now
        }

        void add_breadcrumb (const sm::vec<>& bc_location)
        {
            if (this->isvp == nullptr) { return; }

            ++this->move_counter;
            if (this->breadcrumb_coords.size() < this->isvp->max_instances) {
                this->breadcrumb_coords.push_back (bc_location);
                this->breadcrumb_data.push_back (0.0f); // dummy for now
            } else {
                this->breadcrumb_coords[move_counter % this->isvp->max_instances] = bc_location;
                // breadcrumb_data.push_back (0.0f); // dummy for now, to be flags.
            }
            if (this->bc_clr.empty() || this->bc_alpha.empty() || this->bc_scale.empty()) {
                this->isvp->set_instance_data (this->breadcrumb_coords);
            } else {
                this->isvp->set_instance_data (this->breadcrumb_coords, this->bc_clr, this->bc_alpha, this->bc_scale);
            }
        }

        // Get access to the landscape VisualModel by searching for a selection of model names
        //
        // \param search_names A comma-separated list of model names to search for. If multiple in
        // the list are present, match the first in the list.
        void find_landscape (const std::string& search_names)
        {
            constexpr bool debug_landscape = false;

            std::vector<std::string> names = mplot::tools::stringToVector (search_names, ",");

            mplot::VisualModel<glver>* vmp = nullptr;

            // for each name in l_names
            for (auto search_name : names) {

                if (land != nullptr) { break; } // This will correctly cause exit on a second call to this function

                this->init_vm_accessor(); // Using an accessor scheme to loop through all VMs in a scene
                while ((vmp = this->get_next_vm_accessor()) != nullptr) {
                    if (vmp->name == search_name) {
                        this->land = vmp;
                        this->land->make_navmesh (this->basepath);
                        if constexpr (debug_landscape) {
                            // Can add a NormalsVisual for debug
                            auto nrm = std::make_unique<mplot::NormalsVisual<glver>> (land);
                            nrm->set_parent (this->get_id());
                            nrm->scale_factor = 0.01f;
                            // Set options to show just the boundary edge
                            nrm->options.set (mplot::normalsvisual_flags::show_tri_normals, true);
                            nrm->options.set (mplot::normalsvisual_flags::show_gl_normals, false);
                            nrm->options.set (mplot::normalsvisual_flags::show_boundary_halfedges, true);
                            nrm->options.set (mplot::normalsvisual_flags::show_inner_halfedges, false); // Heavy lifting
                            nrm->options.set (mplot::normalsvisual_flags::show_boundary_next, false);
                            nrm->options.set (mplot::normalsvisual_flags::show_boundary_prev, false);
                            nrm->nextprev_offset = sm::vec<float>::uy() * 0.01f;
                            nrm->finalize();
                            this->addVisualModel (nrm);
                        }
                        break;
                    } // else model with that name does not match
                }
            }
        }

        void init_path_from_csv()
        {
            // Initial position comes from first entry in the csv
            std::cout << "Set initial position from csv\n";
            sm::vec<float> nextloc = { this->csv_positions[0][0], 0.0f, this->csv_positions[0][1] };
            nextloc -= sm::vec<>{ 0.5f, 0.0f, 0.5f }; // don't understand this on re-reading
            // Change camspace based on nextloc. nextloc in landscape coords, so cam_nextloc = landscape.location + nextloc;
            sm::vec<float> ltstr = this->land_to_scene.translation();
            sm::vec<float> cam_nextloc = nextloc;
            cam_nextloc[0] += ltstr[0];
            cam_nextloc[2] += ltstr[2]; // update only x and z
            sm::mat<float, 4> cnl;
            cnl.translate (cam_nextloc);
            setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cnl));
            this->move_counter = 1;
        }

        void setup_landscape()
        {
            if (this->land == nullptr) { return; } // should called find_landscape() first

            std::cout << "Landscape name: " << this->land->name << " was found [" << (land->vpos_size() / 3) << " vertices]\n";
            this->land_to_scene = land->getViewMatrix();
            sm::mat<float, 4> camspace = mplot::compoundray::getCameraSpace (scene);

            if (this->sim_opts.test (craysim::options::path_from_csv) && !this->csv_positions.empty()) {
                this->init_path_from_csv();
            }

            auto[hp_scene, _ti0] = this->land->navmesh->find_triangle_hit (camspace, this->land_to_scene, 100.0f);
            if (_ti0 != std::numeric_limits<std::uint32_t>::max()) {
                // Set up our camera using the data obtained from find_triangle_hit()
                sm::mat<float, 4> cam_to_scene = this->land->navmesh->position_camera (hp_scene, this->land_to_scene, this->hoverheight);
                if (cam_to_scene != sm::mat<float, 4>::identity()) {
                    std::cout << "Set camera pose matrix from\n" << cam_to_scene << std::endl;
                    setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cam_to_scene));
                } else {
                    std::cout << "cam_to_scene is identity??\n";
                }
            } else {
                std::cout << "Failed to find the landscape; Camera position unchanged from glTF/CSV\n";
            }

            sm::mat<float, 4> _cam_to_scene = mplot::compoundray::getCameraSpace (scene);
            std::cout << "Got camera pose matrix from scene:\n" << _cam_to_scene << std::endl;
            sm::vec<float> _lastloc = _cam_to_scene.translation();

            // Update initial_camera_space
            this->initial_camera_space.set_identity();
            this->initial_camera_space.translate (_lastloc);
        }

        // Reset the camera location
        void check_reset_camspace (sm::mat<float, 4>& cam_to_scene)
        {
            // reset to initial camera space if requested
            if (this->vstate.test (state::campose_reset_request) == true) {
                this->stop(); // cancel any active movements
                setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (this->initial_camera_space));

                this->clear_breadcrumbs();
                if (this->sim_opts.test (craysim::options::path_from_csv) && !this->csv_positions.empty()) {
                    this->init_path_from_csv();
                }

                sm::mat<float, 4> camspace = mplot::compoundray::getCameraSpace (scene);
                auto[hp_scene, _ti0] = this->land->navmesh->find_triangle_hit (camspace, this->land_to_scene);
                cam_to_scene = this->land->navmesh->position_camera (hp_scene, this->land_to_scene, this->hoverheight);
                setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cam_to_scene));
                this->vstate.reset (state::campose_reset_request);
                // t-1 values:
                this->tm1_ti0 = _ti0;
                this->tm1_mv_camframe = {};
                this->tm1_cam_to_scene = cam_to_scene;
            }
        }

        // Detect changes in the compound-ray camera, and update all our EyeVisuals accordingly
        void detect_camera_changes (std::vector<mplot::compoundray::EyeVisual<glver>*>& other_eyes)
        {
            if (this->eye == nullptr) { return; }

            std::size_t curr_eye_size = this->last_eye_size;
            // Detect changes in the camera and update eye model as necessary
            if (this->ommatidia_data.size() == 0) {
                if (isCompoundEyeActive()) { getCameraData (this->ommatidia_data); }
            } // else no need to re-get data

            // Update eyevm model (or just update colours)
            this->eye->ommatidia = this->get_ommatidia_ptr();
            for (auto oe : other_eyes) { oe->ommatidia = this->get_ommatidia_ptr(); }

            if (this->ommatidia != nullptr) {
                curr_eye_size = this->ommatidia->size();
                if (curr_eye_size != this->last_eye_size) {
                    this->eye->reinit();
                    for (auto oe : other_eyes) { oe->reinit(); }
                    this->last_eye_size = curr_eye_size;
                } else {
                    this->eye->reinitColours();
                    for (auto oe : other_eyes) { oe->reinitColours(); } // 4x faster to just reinitColours
                }
            }
        }

        // Obtain the current heading of the camera, with respect to the world/scene axes, where
        // Visual::scene_right is North and Visual::scene_out is East.
        // Return result in radians in range [0 2pi], with 0=N, pi/2=E, pi=S, 3pi/2=W.
        float get_compass_heading_rad() const
        {
            // What's the orientation of cam_to_scene wrt to scene? It's just the rotation of cam_to_scene.
            sm::mat<float, 4> cam_to_scene = mplot::compoundray::getCameraSpace (scene);
            // Offset cam_to_scene back to origin
            cam_to_scene.pretranslate (-cam_to_scene.translation());
            // The camera's forwards direction is its z axis
            sm::vec<float> cam_fwds = (cam_to_scene * sm::vec<float>::uz()).less_one_dim();
            // Project this onto the scene ground plane
            sm::vec<float> cam_fwds_proj = sm::geometry::vector_plane_projection (this->scene_up, cam_fwds);
            // Now convert cam_fwds_proj to a 2D angle, using scene_right for N and scene_out for E.
            // Choose a 2D heading vector so that the vector's 2D angle matches the angle used on a
            // compass, which is 0 deg for N and +90 deg for E.
            sm::vec<float, 2> heading2d = {
                cam_fwds_proj.dot (this->scene_right), // N along 2D x axis
                cam_fwds_proj.dot (this->scene_out)    // E along 2D y axis
            };
            float heading_r = heading2d.angle();
            sm::algo::zero_to_twopi (heading_r);
            return heading_r;
        }

        // Returns the compass heading in degrees, as on a regular compass, with 0 degrees meaning N
        // and 90 degrees E.
        float get_compass_heading() const
        {
            return this->get_compass_heading_rad() * sm::mathconst<float>::rad2deg;
        }

        std::string compass_degrees_to_str (float deg) const
        {
            constexpr float sdha = 22.5f / 2.0f; // single direction half angle
            // The 'd' means divider
            constexpr float Nd = 0.0f + sdha;
            constexpr float NNEd = 22.5f + sdha;
            constexpr float NEd = 45.0f + sdha;
            constexpr float ENEd = 67.5f + sdha;
            constexpr float Ed = 90.0f + sdha;
            constexpr float ESEd = 112.5f + sdha;
            constexpr float SEd = 135.0f + sdha;
            constexpr float SSEd = 157.5f + sdha;
            constexpr float Sd = 180.0f + sdha;
            constexpr float SSWd = 202.5f + sdha;
            constexpr float SWd = 225.0f + sdha;
            constexpr float WSWd = 247.5f + sdha;
            constexpr float Wd = 270.0f + sdha;
            constexpr float WNWd = 292.5f + sdha;
            constexpr float NWd = 315.0f + sdha;
            constexpr float NNWd = 337.5f + sdha;

            std::string dir = "";
            if (deg > NNWd) { dir = "N"; }
            else if (deg > NWd) { dir = "NNW"; }
            else if (deg > WNWd) { dir = "NW"; }
            else if (deg > Wd) { dir = "WNW"; }
            else if (deg > WSWd) { dir = "W"; }
            else if (deg > SWd) { dir = "WSW"; }
            else if (deg > SSWd) { dir = "SW"; }
            else if (deg > Sd) { dir = "SSW"; }
            else if (deg > SSEd) { dir = "S"; }
            else if (deg > SEd) { dir = "SSE"; }
            else if (deg > ESEd) { dir = "SE"; }
            else if (deg > Ed) { dir = "ESE"; }
            else if (deg > ENEd) { dir = "E"; }
            else if (deg > NEd) { dir = "ENE"; }
            else if (deg > NNEd) { dir = "NE"; }
            else if (deg > Nd) { dir = "NNE"; }
            else { dir = "N"; }
            return dir;
        }

        // Return 'N' or 'NNE' or equivalent for the heading direction
        std::string get_compass_heading_str() const
        {
            return this->compass_degrees_to_str (this->get_compass_heading());
        }

        void api_move_over_land()
        {
            // Check vec/quat/matrix and then make mv_camframe
            rotateCamerasLocallyAround (this->api_cam_rotn_angle,
                                        this->api_cam_rotn_axis[0],
                                        this->api_cam_rotn_axis[1],
                                        this->api_cam_rotn_axis[2]);

            sm::mat<float, 4> cam_to_scene = mplot::compoundray::getCameraSpace (scene);

            // move by this->api_cam_mv; along z (for now?)
            sm::vec<float> mv_camframe = this->api_cam_mv;
            sm::vec<float> lastloc = cam_to_scene.translation();
            sm::mat<float, 4> cam_to_scene_sv = cam_to_scene;
            std::uint32_t ti0_sv = this->land->navmesh->ti0;
            try {
                cam_to_scene = this->land->navmesh->compute_mesh_movement (mv_camframe, cam_to_scene, this->land_to_scene, this->hoverheight);
                // Now we have moved, can compute instantaneous velocity
                this->instantaneous_velocity = cam_to_scene.translation() - cam_to_scene_sv.translation();

                this->tm1_ti0 = ti0_sv;
                this->tm1_mv_camframe = mv_camframe;
                this->tm1_cam_to_scene = cam_to_scene_sv;

            } catch (const std::exception& e) {
                std::string msg (e.what());
                std::cout << "Exception: " << msg << std::endl;
                if (msg.find ("off-edge:") == 0) {
                    std::cout << "We went off the edge. Key move not possible. Don't crash.\n";
                    this->land->navmesh->ti0 = ti0_sv;
                } else {
                    std::cout << "key-command move was not possible...\n";
                    {
                        // Duplicated code from key_move...
                        std::cout << "Saving compute_mesh_movement data\n";
                        std::cout << "mv_camframe: " << mv_camframe << " and tm1_mv_camframe: " << this->tm1_mv_camframe << std::endl;
                        std::cout << "cam_to_scene_sv is\n" << cam_to_scene_sv
                                  << "\nand tm1_cam_to_scene:\n" << this->tm1_cam_to_scene << std::endl;
                        sm::hdfdata dsv ("./craysim.h5", std::ios::out | std::ios::trunc);
                        dsv.add_contained_vals ("/mv_camframe", mv_camframe);
                        dsv.add_contained_vals ("/cam_to_scene", cam_to_scene_sv.arr);
                        dsv.add_contained_vals ("/land_to_scene", this->land_to_scene.arr);
                        dsv.add_val ("/hoverheight", this->hoverheight);
                        dsv.add_val ("/ti0", ti0_sv);
                        // Also save t-1 values:
                        dsv.add_contained_vals ("/tm1_mv_camframe", this->tm1_mv_camframe);
                        dsv.add_contained_vals ("/tm1_cam_to_scene", this->tm1_cam_to_scene.arr);
                        dsv.add_val ("/tm1_ti0", this->tm1_ti0);
                    }
                    throw e;
                }
            }

            setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cam_to_scene));

            this->add_breadcrumb (lastloc);

            if (this->eye != nullptr) { this->eye->setViewMatrix (cam_to_scene); }
            if (this->agent_body != nullptr) { this->agent_body->setViewMatrix (cam_to_scene); }
            this->agent_coords->setViewMatrix (cam_to_scene);
        }

        // Make a keyboard based movement over the landscape
        void key_move_over_land (const float fps)
        {
            sm::mat<float, 4> cam_to_scene = mplot::compoundray::getCameraSpace (scene);
            if (this->is_actively_rotating()) {
                // Up-down (pitch) is rotation about local camera frame axis x
                rotateCamerasLocallyAround (this->get_vertical_rotation_angle(), 1.0f, 0.0f, 0.0f);
                // Left-and-right (yaw) is rotation about local camera frame axis y
                rotateCamerasLocallyAround (this->get_horizontal_rotation_angle(), 0.0f, 1.0f, 0.0f);
                // Roll
                rotateCamerasLocallyAround (this->get_roll_rotation_angle(), 0.0f, 0.0f, 1.0f);
                cam_to_scene = mplot::compoundray::getCameraSpace (scene); // update
            }
            if (this->is_actively_translating()) {
                if (this->move_state.test (craysim::visual<glver>::move_sense::up)) {
                    this->hoverheight += 0.0001f;
                } else if (this->move_state.test (craysim::visual<glver>::move_sense::down)) {
                    this->hoverheight -= 0.0001f;
                    if (this->hoverheight < 0.0f) { this->hoverheight = 0.0f; }
                }
                sm::vec<float> mv_camframe = this->get_movement_vector (fps);
                sm::vec<float> lastloc = cam_to_scene.translation();
                sm::mat<float, 4> cam_to_scene_sv = cam_to_scene;
                std::uint32_t ti0_sv = this->land->navmesh->ti0;
                try {
                    cam_to_scene = this->land->navmesh->compute_mesh_movement (mv_camframe, cam_to_scene, this->land_to_scene, this->hoverheight);
                    // Now we have moved, can compute instantaneous velocity
                    this->instantaneous_velocity = cam_to_scene.translation() - cam_to_scene_sv.translation();

                    this->tm1_ti0 = ti0_sv;
                    this->tm1_mv_camframe = mv_camframe;
                    this->tm1_cam_to_scene = cam_to_scene_sv;

                } catch (const std::exception& e) {
                    std::string msg (e.what());
                    std::cout << "Exception: " << msg << std::endl;
                    if (msg.find ("off-edge:") == 0) {
                        std::cout << "We went off the edge. Key move not possible. Don't crash.\n";
                        this->land->navmesh->ti0 = ti0_sv;
                    } else {
                        std::cout << "key-command move was not possible...\n";
                        {
                            std::cout << "Saving compute_mesh_movement data\n";
                            std::cout << "mv_camframe: " << mv_camframe << " and tm1_mv_camframe: " << this->tm1_mv_camframe << std::endl;
                            std::cout << "cam_to_scene_sv is\n" << cam_to_scene_sv
                                      << "\nand tm1_cam_to_scene:\n" << this->tm1_cam_to_scene << std::endl;
                            sm::hdfdata dsv ("./craysim.h5", std::ios::out | std::ios::trunc);
                            dsv.add_contained_vals ("/mv_camframe", mv_camframe);
                            dsv.add_contained_vals ("/cam_to_scene", cam_to_scene_sv.arr);
                            dsv.add_contained_vals ("/land_to_scene", this->land_to_scene.arr);
                            dsv.add_val ("/hoverheight", this->hoverheight);
                            dsv.add_val ("/ti0", ti0_sv);
                            // Also save t-1 values:
                            dsv.add_contained_vals ("/tm1_mv_camframe", this->tm1_mv_camframe);
                            dsv.add_contained_vals ("/tm1_cam_to_scene", this->tm1_cam_to_scene.arr);
                            dsv.add_val ("/tm1_ti0", this->tm1_ti0);
                        }
                        throw e;
                    }
                }

                setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cam_to_scene));

                this->add_breadcrumb (lastloc);
            }
            this->check_reset_camspace (cam_to_scene); // if requested
            // Update the view matrix of eye and eye localspace axes
            if (this->eye != nullptr) { this->eye->setViewMatrix (cam_to_scene); }
            if (this->agent_body != nullptr) { this->agent_body->setViewMatrix (cam_to_scene); }
            this->agent_coords->setViewMatrix (cam_to_scene);
        }

        void walk_over_land (const float fps)
        {
            sm::mat<float, 4> cam_to_scene = mplot::compoundray::getCameraSpace (scene);

            // A random walk mode
            if (!this->rrg || this->vstate.test (craysim::visual<glver>::state::walk) == false) { return; }

            // set rotation and step length according to the Stone paper
            this->rrg->step();
            // rrg.omega is the angular speed rrg.speed is the linear speed
            rotateCamerasLocallyAround (this->rrg->omega, 0.0f, 1.0f, 0.0f);
            cam_to_scene = mplot::compoundray::getCameraSpace (scene);
            // ti0, mv_camframe, cam_to_scene to save.
            sm::vec<float> mv_camframe = { 0, 0, this->rrg->speed };
            sm::mat<float, 4> cam_to_scene_sv = cam_to_scene;
            std::uint32_t ti0_sv = this->land->navmesh->ti0;
            try {
                cam_to_scene = this->land->navmesh->compute_mesh_movement (mv_camframe, cam_to_scene, this->land_to_scene, this->hoverheight);
                // Now we have moved, can compute instantaneous velocity
                this->instantaneous_velocity = cam_to_scene.translation() - cam_to_scene_sv.translation();

                this->tm1_ti0 = ti0_sv;
                this->tm1_mv_camframe = mv_camframe;
                this->tm1_cam_to_scene = cam_to_scene_sv;
                this->add_breadcrumb (cam_to_scene_sv.translation());

            } catch (const std::exception& e) {
                std::string msg (e.what());
                std::cout << "Exception: " << msg << std::endl;
                if (msg.find ("off-edge:") == 0) {
                    std::cout << "We went off the edge. Change direction (rrg->about_turn()).\n";
                    this->rrg->about_turn();
                    this->land->navmesh->ti0 = ti0_sv;
                } else {
                    this->sim_opts.set (craysim::options::max_fps, false); // don't burn electricity after exception
                    this->vstate.set (craysim::visual<glver>::state::walk, false);
                    {
                        std::cout << "Saving compute_mesh_movement data\n"
                                  << "mv_camframe: " << mv_camframe << " and tm1_mv_camframe: " << this->tm1_mv_camframe
                                  << "\ncam_to_scene_sv is\n" << cam_to_scene_sv
                                  << "\nand tm1_cam_to_scene:\n" << this->tm1_cam_to_scene << std::endl;
                        sm::hdfdata dsv ("./craysim.h5", std::ios::out | std::ios::trunc);
                        dsv.add_contained_vals ("/mv_camframe", mv_camframe);
                        dsv.add_contained_vals ("/cam_to_scene", cam_to_scene_sv.arr);
                        dsv.add_contained_vals ("/land_to_scene", this->land_to_scene.arr);
                        dsv.add_val ("/hoverheight", this->hoverheight);
                        dsv.add_val ("/ti0", ti0_sv);
                        // Also save t-1 values:
                        dsv.add_contained_vals ("/tm1_mv_camframe", this->tm1_mv_camframe);
                        dsv.add_contained_vals ("/tm1_cam_to_scene", this->tm1_cam_to_scene.arr);
                        dsv.add_val ("/tm1_ti0", this->tm1_ti0);
                    }
                    throw e;
                }
            }
            setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cam_to_scene));
            this->check_reset_camspace (cam_to_scene); // if requested
            // Update the view matrix of eye and eye localspace axes
            if (this->eye != nullptr) { this->eye->setViewMatrix (cam_to_scene); }
            if (this->agent_body != nullptr) { this->agent_body->setViewMatrix (cam_to_scene); }
            this->agent_coords->setViewMatrix (cam_to_scene);
        }

        bool csv_playback (const float fps)
        {
            bool rtn = true;

            sm::mat<float, 4> cam_to_scene = mplot::compoundray::getCameraSpace (scene);

            if (this->csv_positions.size() > this->move_counter) {

                if (this->directions.contains (this->move_counter)) {
                    this->setCurrentDirectionEvent (this->directions[this->move_counter]);
                }

                /*
                 * With a csv path, teleport between each location (and then estimate the heading of
                 * the ant, or use csv_dirns). CSV positions are relative to the landscape model.
                 */
                sm::vec<float> lastcamloc = cam_to_scene.translation();

                sm::vec<float> nextloc = { this->csv_positions[this->move_counter][0], 0, this->csv_positions[this->move_counter][1] };
                sm::vec<float> lastloc = { this->csv_positions[this->move_counter - 1][0], 0, this->csv_positions[this->move_counter - 1][1] };
                //std::cout << "Teleport a distance " << (lastloc - nextloc).length() << std::endl;

                sm::vec<float> fwds;
                if (!this->csv_dirns.empty()) {
                    // Prolly a bit hacky. Does not take account of this->scene_up.
                    fwds = { this->csv_dirns[this->move_counter][0], 0.0f, this->csv_dirns[this->move_counter][1] };
                } else {
                    fwds = nextloc - lastloc;
                }

                if (fwds.length() > 0.0f) {
                    sm::vec<float> ltstr = this->land_to_scene.translation(); // always the same
                    sm::vec<float> cam_nextloc = nextloc;
                    cam_nextloc[0] += ltstr[0];
                    cam_nextloc[2] += ltstr[2]; // update only x and z
                    //std::cout << "--> cam_nextloc: " << cam_nextloc << std::endl;

                    sm::mat<float, 4> cnl;
                    cnl.translate (cam_nextloc);
                    setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cnl));
                    cam_to_scene = mplot::compoundray::getCameraSpace (scene);
                }

                // Find triangle hits using the scene's 'up' direction.
                sm::vec<float> camloc_mf = (this->land_to_scene.inverse() * cam_to_scene).translation();
                sm::vec<float> vnrm = this->scene_up;
                vnrm *= 4.0f;
                auto[hp_scene, _ti0] = this->land->navmesh->find_triangle_hit (this->land_to_scene, camloc_mf + (vnrm / 2.0f), -2.0f * vnrm, this->last_ti);
                this->last_ti = _ti0;
                //std::cout << "--> Got hp_scene: " << hp_scene << std::endl;

                if (_ti0 != std::numeric_limits<std::uint32_t>::max()) {

                    // Set up our camera using the data obtained from find_triangle_hit()
                    if (fwds.length() > 0.0f) {
                        cam_to_scene = this->land->navmesh->position_camera (hp_scene, this->land_to_scene, this->hoverheight, fwds);
                    } // else agent position has not changed from the last time.


                    if (cam_to_scene != sm::mat<float, 4>::identity()) {
                        setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (cam_to_scene));
                    } else { std::cout << "cam_to_scene is identity?!\n"; }
                    // else what to do if cam_to_scene is identity?

                    // Now we have moved, can compute instantaneous velocity
                    this->instantaneous_velocity = cam_to_scene.translation() - lastcamloc;
                    //std::cout << "instant vel: " << this->instantaneous_velocity
                    //          << "   cam_to_scene fwds = "
                    //          << (cam_to_scene * sm::vec<>::uz()) << std::endl;

                } else {
                    // Rather than throwing, could just move on to next in csv?
                    cam_to_scene = mplot::compoundray::getCameraSpace (scene);
                    std::cout << "Omit csv_positions[this->move_counter] = csv_positions[" << this->move_counter << "] = "
                              << this->csv_positions[this->move_counter] << " (failed to find triangle hit)\n";
                }

                this->add_breadcrumb (lastcamloc); // increments move_counter

            } else { // no more movements
                rtn = false;
            }

            this->check_reset_camspace (cam_to_scene); // if requested
            // Update the view matrix of eye and eye localspace axes
            if (this->eye != nullptr) { this->eye->setViewMatrix (cam_to_scene); }
            if (this->agent_body != nullptr) { this->agent_body->setViewMatrix (cam_to_scene); }
            this->agent_coords->setViewMatrix (cam_to_scene);

            return rtn;
        };

        // Debug a previously saved crash movement
        void do_crashed_movement ()
        {
            std::cout << "Loading compute_mesh_movement data from crash file\n";
            sm::mat<float, 4> _cam_to_scene = {{}};
            sm::mat<float, 4> _land_to_scene = {{}};
            sm::vec<float> _mv_camframe = {};
            float _hoverheight = 0.0f;
            std::uint32_t _ti0 = 0u;

            sm::hdfdata dsv ("./craysim.h5", std::ios::in);
            dsv.read_contained_vals ("/mv_camframe", _mv_camframe);
            dsv.read_contained_vals ("/cam_to_scene", _cam_to_scene.arr);
            dsv.read_contained_vals ("/land_to_scene", _land_to_scene.arr);
            dsv.read_val ("/hoverheight", _hoverheight);
            dsv.read_val ("/ti0", _ti0);
            dsv.read_contained_vals ("/tm1_mv_camframe", this->tm1_mv_camframe);
            dsv.read_contained_vals ("/tm1_cam_to_scene", this->tm1_cam_to_scene.arr);
            dsv.read_val ("/tm1_ti0", this->tm1_ti0);

            setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (this->tm1_cam_to_scene));
            if (this->eye != nullptr) { this->eye->setViewMatrix (this->tm1_cam_to_scene); }
            if (this->agent_body != nullptr) { this->agent_body->setViewMatrix (this->tm1_cam_to_scene); }
            this->agent_coords->setViewMatrix (this->tm1_cam_to_scene);
            std::cout << "First compute_mesh_movement from saved data:\n";
            this->land->navmesh->ti0 = this->tm1_ti0;
            sm::mat<float, 4> _cam_to_scene_1 = this->land->navmesh->compute_mesh_movement (this->tm1_mv_camframe, this->tm1_cam_to_scene, _land_to_scene, _hoverheight);
            std::cout << "\ncompute_mesh_movement for time t-1 returned cam_to_scene:\n" << _cam_to_scene_1 << "\n";
            //if (_cam_to_scene_1 != _cam_to_scene) { Random walk may have rotated the camera, to further alter cam_to_scene }
            std::cout << "Running second compute_mesh_movement from saved data:\n";
            this->land->navmesh->ti0 = _ti0;
            _cam_to_scene = this->land->navmesh->compute_mesh_movement (_mv_camframe, _cam_to_scene, _land_to_scene, _hoverheight);
            std::cout << "compute_mesh_movement for time t returned!\n";
            // Set the new position for camera and ant models
            setCameraPoseMatrix (mplot::compoundray::mat44_to_Matrix4x4 (_cam_to_scene));
            if (this->eye != nullptr) { this->eye->setViewMatrix (_cam_to_scene); }
            if (this->agent_body != nullptr) { this->agent_body->setViewMatrix (_cam_to_scene); }
            this->agent_coords->setViewMatrix (_cam_to_scene);
        }

        void start_loop_timer()
        {
            this->fps_profiler.at_begin (craysim::best_n_samples (getCurrentEyeSamplesPerOmmatidium()));
        }

        void end_loop_timer() { this->fps_profiler.at_end(); }

        // Allows client code to set up other windows etc
        std::vector<mplot::Visual<glver>*> other_windows = {};
        std::vector<mplot::compoundray::EyeVisual<glver>*> other_eyes = {};
        std::vector<mplot::Visual<glver>*> slow_windows = {};
        // How many fast renders to wait until we re-render the slow windows?
        std::uint64_t slow_every = 10u;

        // Time const for frames.
        double frame_tau = 0.0167;

        // Call this from your main loop. Returns true if slow windows were processed
        bool render_and_poll ()
        {
            ++this->render_counter;

            // The current camera may have changed, this subroutine deals with any changes in this->eye and other_eyes
            this->detect_camera_changes (this->other_eyes); // reinits the eyes

            // Now render the mathplot window
            this->render();
            // Change label after render (it needs v's context, not any of the other windows)
            if (this->move_counter % 100 == 0) { this->fps_label_update(); }

            // Save some electricity while developing - limit to 60 FPS. For max speed use this->poll() (-x)
            if (this->sim_opts.test (craysim::options::max_fps)) { this->poll(); } else { this->wait (this->frame_tau); }

            // Render the other windows
            if ((this->render_counter % this->slow_every) == 0u) {
                for (auto swin : this->slow_windows) { swin->render(); }
            }
            for (auto owin : this->other_windows) { owin->render(); }

            // Deal with any movements commanded by key press events (including reset)

            this->setContext(); // right now key move over land needs main window's context

            this->instantaneous_velocity = {}; // velocity computed per render cycle

            this->agent_coords->setHide (!this->vstate.test(craysim::visual<glver>::state::show_camframe));

            // Any of the following movement-creating functions will set the instantaneous velocity
            //
            // walk/csv playback/check keys for movement command
            if (this->vstate.test (craysim::visual<glver>::state::paused) == false) {

                if (this->vstate.test (craysim::visual<glver>::state::walk)) {
                    this->walk_over_land (this->fps_profiler.fps_mean);
                } else if (this->sim_opts.test (craysim::options::path_from_csv) && this->csv_positions.size() > this->move_counter) {
                    // Construct path from csv file of 2D agent locations
                    this->csv_playback (this->fps_profiler.fps_mean);
                } else if (this->sim_opts.any_of ({craysim::options::api_movement, craysim::options::homing_mode})) {
                    // React to movements commanded by vec/quaternion or transformation matrix
                    // (i.e. by client code).
                    this->api_move_over_land();
                } else {
                    this->key_move_over_land (this->fps_profiler.fps_mean);
                }
            }

            // Call the compound-ray ray casting method to recompute the compound-eye view of the scene
            renderFrame();
            // Access data so that a brain model could be fed
            if (isCompoundEyeActive()) {
                getCameraData (this->ommatidia_data);
                this->ommatidia = &scene->m_ommVecs[scene->getCameraIndex()];

                // if csv mode, then save the data
                if (this->sim_opts.all_of ({craysim::options::path_from_csv, craysim::options::save_hdf5})) {
                    std::cout << "Saving frame...\n";
                    std::string ommframe = "/ommatidia_data/frame_" + std::to_string (this->move_counter);
                    try {
                        record.add_contained_vals (ommframe.c_str(), this->ommatidia_data);
                    } catch (const std::exception& e) {} // Probably didn't move this time.
                }
            }

            // Scale size of breadcrumbs based on distance
            float iscl = 1.0f * std::log (1.0f + this->get_d_to_rotation_centre());
            this->isvp->set_instance_scale (iscl);

            return (this->render_counter % this->slow_every) == 0u;
        }

        // Save once-only data into the recording file (ommatidia data)
        void complete_recording()
        {
            if (this->sim_opts.test (craysim::options::path_from_csv)) {
                // convert std::vector<Ommatidium>* ommatidia into vvecs that can be h5 saved
                auto ommat = this->get_ommatidia_ptr();
                sm::vvec<sm::vec<float, 3>> o_pos;
                sm::vvec<sm::vec<float, 3>> o_dir;
                sm::vvec<float> o_aa;
                sm::vvec<float> o_fo;
                for (auto o : *ommat) {
                    o_pos.push_back (o.relativePosition);
                    o_dir.push_back (o.relativeDirection);
                    o_aa.push_back (o.acceptanceAngleRadians);
                    o_fo.push_back (o.focalPointOffset);
                }
                std::cout << "Pos\n";
                this->record.add_contained_vals ("/ommatidia/relativePosition", o_pos);
                std::cout << "Dir\n";
                this->record.add_contained_vals ("/ommatidia/relativeDirection", o_dir);
                std::cout << "AA\n";
                this->record.add_contained_vals ("/ommatidia/acceptanceAngleRadians", o_aa);
                std::cout << "FO\n";
                this->record.add_contained_vals ("/ommatidia/focalPointOffset", o_fo);
            }
        }

        void set_hoverheight (const std::string& cmd_line_str, const float default_height = 0.01f)
        {
            this->hoverheight = default_height;
            if (!cmd_line_str.empty()) {
                this->hoverheight = std::atof (cmd_line_str.c_str());
                std::cout << "Set user-supplied hoverheight to " << this->hoverheight << std::endl;
            }
        }

        void fps_label_update()
        {
            this->fps_label->setupText (this->fps_profiler.fps_txt + std::string(" "));
        }

        std::vector<mplot::compoundray::Ommatidium>* get_ommatidia_ptr()
        {
            return reinterpret_cast<std::vector<mplot::compoundray::Ommatidium>*>(ommatidia);
        }

        mplot::meshgroup* get_head_mesh()
        {
            return this->oces_reader.read_success ? reinterpret_cast<mplot::meshgroup*>(&this->oces_reader.head_mesh) : nullptr;
        }

        // Get the transform matrix defining the pose of the camera/agent. That's stored in agent_coords
        sm::mat<float, 4> get_camera_pose() const { return this->agent_coords->getViewMatrix(); }

        /*
         * Is the home location to the left of the agent/camera?
         *
         * If scene_up is uy(), then 3d x axis is north. The 3d z axis maps to the 2d x axis and the
         * 3d x axis maps to the 2d y axis.
         *
         * If scene_up is uz(), then 3d y axis is north. The 3d x axis maps to 2d x axis and 3d y
         * axis maps to 2d y axis.
         *
         * \param home_Index The index into craysim::visual::home_locations from which to determine
         * left-ness/right-ness
         *
         * \return true if location is to the left of the camera, else false
         */
        bool home_location_is_on_left (const std::uint32_t home_index = 0) const
        {
            if (home_index >= this->home_locations.size()) { return false; }
            const float head_rad = this->get_compass_heading_rad();
            const sm::vec<float> to_nest = this->home_locations[home_index] - this->get_camera_pose().translation();
            const sm::vec<float> vec_to_nest = sm::geometry::vector_plane_projection (this->scene_up, to_nest);
            // Convert compass heading into a 2D vector
            const float head_rad_2 = sm::mathconst<float>::pi_over_2 - head_rad;
            const sm::vec<float, 2> head_vec_2 = { std::cos (head_rad_2), std::sin (head_rad_2) };
            // Make a 2D vector to the nest
            sm::vec<float, 2> to_nest_2 = {};
            if (this->scene_up == sm::vec<float>::uy()) {
                to_nest_2 = { vec_to_nest[2], vec_to_nest[0] };
            } else if (this->scene_up == sm::vec<float>::uz()) {
                to_nest_2 = { vec_to_nest[0], vec_to_nest[1] };
            } else {
            }
            // Compute the angle to the nest
            float angle_to_nest = head_vec_2.angle() - to_nest_2.angle();
            sm::algo::minus_pi_to_pi (angle_to_nest);
            // Angle gives left-ness
            if (angle_to_nest > 0.0f) { return false; }
            return true;
        }

        /*!
         * Return instantaneous velocity in the 2 dimensional plane defined by scene_up.
         *
         * If scene_up is uy(), then 3d x axis is north. The 3d z axis maps to the 2d x axis and the
         * 3d x axis maps to the 2d y axis.
         *
         * If scene_up is uz(), then 3d y axis is north. The 3d x axis maps to 2d x axis and 3d y
         * axis maps to 2d y axis.
         */
        sm::vec<float, 2> get_velocity_2d () const
        {
            const sm::vec<float> inst_v_projected = sm::geometry::vector_plane_projection (this->scene_up, this->instantaneous_velocity);
            if (this->scene_up == sm::vec<float>::uy()) {
                return sm::vec<float, 2>{ inst_v_projected[2], inst_v_projected[0] };
            } else if (this->scene_up == sm::vec<float>::uz()) {
                return sm::vec<float, 2>{ inst_v_projected[0], inst_v_projected[1] };
            } else {
                throw std::runtime_error ("craysim::visual::get_velocity_2d: Don't know what to do unless scene_up is uy() or uz()");
            }
        }

        // Our sim options.
        sm::flags<craysim::options> sim_opts;
        // A member fps_profiler
        mplot::fps::profiler fps_profiler;
        // The FPS label, accessible to client code
        mplot::VisualTextModel<glver>* fps_label;
        // Base path for glTF file
        std::string basepath = {};
        // Full path for glTF file
        std::string path = {};
        // The eye file path, obtained from OCES file
        std::string efpath = {};
        // Open Compound Eye Standard reader used to access an agent head mesh (compound-ray reads the ommatidia info)
        oces::reader oces_reader;
        // Required in every craysim, I think. craysim::state? member of craysim::visual?
        std::vector<std::array<float, 3>> ommatidia_data;
        std::vector<Ommatidium>* ommatidia = nullptr;
        // This is the start position of the camera as loaded from the gltf or as first located 'on the landscape'
        sm::mat<float, 4> initial_camera_space;

        // An mplot::VisualModel of the compound-ray eye
        mplot::compoundray::EyeVisual<glver>* eye = nullptr;
        // You may have a VisualModel of an 'agent body' to go another with your eye's EyeVisual
        mplot::VisualModel<glver>* agent_body = nullptr;
        // A coordinate arrow frame to show location of compound-ray eye/agent_body (in case they are tiny)
        mplot::CoordArrows<glver>* agent_coords = nullptr;

        // Visualization of a breadcrumb trail
        mplot::InstancedScatterVisual<glver>* isvp = nullptr;
        // Track how many calls to render have been made. At 1000 FPS this overflows at 10^17 seconds which is about 10^9 years.
        std::uint64_t render_counter = 0u;
        // State for breadcrumb trail. A move counter
        std::uint64_t move_counter = 0u;
        // Container for breadcrumb locations
        sm::vvec<sm::vec<float, 3>> breadcrumb_coords = {};
        // Container for breadcrumb data (size, colour, alpha, etc)
        sm::vvec<float> breadcrumb_data = {};
        // Breadcrumb colours. May be empty. Set up in your client code
        sm::vvec<std::array<float, 3>> bc_clr;
        // Breadcrumb alpha values. May be empty. Set up in your client code
        sm::vvec<float> bc_alpha;
        // Breadcrumb scale values. May be empty. Set up in your client code
        sm::vvec<float> bc_scale;

        // Client code gives us names of the navigation landscape. If we find the landscape, store a pointer to it with this
        mplot::VisualModel<glver>* land = nullptr;
        // land's viewmatrix. converts land model to scene
        sm::mat<float, 4> land_to_scene;
        // We can load data from a csv file for pre-defined paths. Client code populates these.
        sm::vvec<sm::vec<float, 2>> csv_positions;
        sm::vvec<sm::vec<float, 2>> csv_dirns;
        sm::vvec<std::uint32_t> csv_flags;
        // Home/nest locations. Client code can populate this and make use of it in any way that is useful.
        sm::vvec<sm::vec<float>> home_locations;
        // It may be useful to define some target locations, too.
        sm::vvec<sm::vec<float>> target_locations;
        // When reproducing csv paths, it's useful to keep a record of the last triangle, because the
        // most likely next triangle is the last triangle.
        std::uint32_t last_ti = std::numeric_limits<std::uint32_t>::max();
        // This is the height above the landscape to place the camera/agent. Set it suitably in your application.
        float hoverheight = 0.01f;
        // We keep a track of the eye size. Used in detect_camera_changes
        std::size_t last_eye_size = 0u;

        // Random route generation object
        std::unique_ptr<craysim::random_walk<float>> rrg;

        // For debug saving and computation of instantaneous velocity
        sm::mat<float, 4> tm1_cam_to_scene;
        sm::vec<float> tm1_mv_camframe = {};
        std::uint32_t tm1_ti0 = 0u;

        // Recording object
        sm::hdfdata record;// (h5_path, std::ios::out | std::ios::trunc);

        // Defining scripted camera movements. Use a sm::config object to load a json file with a definition
        sm::config film_director;

        // This is populated from film_director.
        std::map<std::uint32_t, mplot::direction_data> directions;

        // Movement state (class and bitset) (flags?)
        enum class move_sense : uint16_t {
            forward, backward, left, right, up, down,
            rot_up, rot_down, rot_left, rot_right, rot_roll_left, rot_roll_right
        };
        sm::flags<move_sense> move_state;

        // Speed of translations commanded by key press (in scene units per second). From this
        // determine distance for one movement step based on current FPS/seconds per frame
        float kcmd_speed = 0.5f;
        // Speed of rotations
        float kcmd_angular_speed = 2.0f * mc::two_pi / 360.0f;

        // The instantaneous velocity arising from the last movement
        sm::vec<float> instantaneous_velocity = {};

        enum class state : uint8_t {
            show_cones,            // Parameter for EyeVisual. Draw simple flared tubes in mathplot window
            campose_reset_request, // A request to reset the pose of the camera
            show_camframe,         // Show camera axes?
            paused,                // Pause sim (i.e. pause time)?
            stepfwd,               // If true and if paused is true, step forward one timestep in the camera input
            walk,                  // If true, do a random walk
            freeze                 // Freeze movement
        };
        sm::flags<state> vstate;

        void freeze (const bool val)
        {
            this->vstate.set (state::freeze, val);
            this->stop();
        }

        // Movement API
        sm::vec<float> api_cam_rotn_axis = this->scene_up;
        float api_cam_rotn_angle = 0.0f;
        sm::vec<float> api_cam_mv = {}; // A movement in the camera's frame. z is forwards.
        void rotate_camera (const sm::vec<float>& _axis, const float _angle)
        {
            this->api_cam_rotn_axis = _axis;
            this->api_cam_rotn_angle = _angle;
        }
        void move_camera (sm::vec<float>& v) { this->api_cam_mv = v; }

        // Get the camera's key-commanded movement vector to give speed in model world at the current FPS
        sm::vec<float, 3> get_movement_vector (const float fps)
        {
            sm::vec<float, 3> output = {};
            if (this->move_state.test (move_sense::up)) { output += 0.1f * kcmd_speed / fps * sm::vec<>::uy(); } // uy is up
            if (this->move_state.test (move_sense::down)) { output += 0.1f * kcmd_speed / fps * -sm::vec<>::uy(); }
            if (this->move_state.test (move_sense::left)) { output += kcmd_speed / fps * sm::vec<>::ux(); }
            if (this->move_state.test (move_sense::right)) { output += kcmd_speed / fps * -sm::vec<>::ux(); }    // right is in -x dirn
            if (this->move_state.test (move_sense::forward)) { output += kcmd_speed / fps * sm::vec<>::uz(); }   // fwd is in uz dirn
            if (this->move_state.test (move_sense::backward)) { output += kcmd_speed / fps * -sm::vec<>::uz(); }
            return output;
        }

        // Get the camera's vertical rotation angle (pitch).
        float get_vertical_rotation_angle()
        {
            float out = 0.0f;
            if (this->move_state.test (move_sense::rot_up)) { out += kcmd_angular_speed; }
            if (this->move_state.test (move_sense::rot_down)) { out -= kcmd_angular_speed; }
            return out;
        }

        // Get the camera's horizontal rotation angle (yaw). Rightward is positive.
        float get_horizontal_rotation_angle()
        {
            float out = 0.0f;
            if (this->move_state.test (move_sense::rot_left)) { out += kcmd_angular_speed; }
            if (this->move_state.test (move_sense::rot_right)) { out -= kcmd_angular_speed; }
            return out;
        }

        // Get the camera's roll
        float get_roll_rotation_angle()
        {
            float out = 0.0f;
            if (this->move_state.test (move_sense::rot_roll_left)) { out -= kcmd_angular_speed; }
            if (this->move_state.test (move_sense::rot_roll_right)) { out += kcmd_angular_speed; }
            return out;
        }

        // Really "do we have a rotation command?"
        bool is_actively_rotating()
        {
            return this->move_state.any_of ({
                    move_sense::rot_up, move_sense::rot_down,
                    move_sense::rot_left, move_sense::rot_right,
                    move_sense::rot_roll_left, move_sense::rot_roll_right });
        }

        // Really "do we have a translation command?"
        bool is_actively_translating()
        {
            return this->move_state.any_of ({
                    move_sense::up, move_sense::down,
                    move_sense::left, move_sense::right,
                    move_sense::forward, move_sense::backward });
        }

        // Is the camera 'actively moving'?
        bool is_actively_moving() { return this->move_state.any(); }

        // Cancel any movement. Also unpause
        void stop()
        {
            this->vstate.reset (state::paused);
            this->move_state.reset();
        }

    protected:

        static constexpr bool debug_callback_extra = false;
        void key_callback_extra (int key, int scancode, int action, int mods) override final
        {
            if (this->vstate.test (state::freeze)) { return; } // Don't respond to movement keys

            // Process press/repeat key actions (none will work with Ctrl or Shift)
            if (action == mplot::keyaction::press && !(mods & mplot::keymod::shift)) {
                if (key == mplot::key::w) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::forward);
                } else if (key == mplot::key::a && !mods) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::left);
                } else if (key == mplot::key::d) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::right);
                } else if (key == mplot::key::s) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::backward);
                } else if (key == mplot::key::p) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::up);
                } else if (key == mplot::key::l) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::down);
                } else if (key == mplot::key::up) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::rot_up);
                } else if (key == mplot::key::down) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::rot_down);
                } else if (key == mplot::key::left) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::rot_left);
                } else if (key == mplot::key::right) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::rot_right);
                } else if (key == mplot::key::comma) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::rot_roll_left);
                } else if (key == mplot::key::period) {
                    this->vstate.reset (state::paused);
                    this->move_state.set (move_sense::rot_roll_right);
                } else if (key == mplot::key::end) {
                    this->kcmd_speed = this->kcmd_speed * 0.5f;
                    std::cout << "Speed reduced to " << this->kcmd_speed  << "m/s" << std::endl;
                } else if (key == mplot::key::home) {
                    this->kcmd_speed = this->kcmd_speed * 2.0f;
                    std::cout << "Speed increased to " << this->kcmd_speed  << "m/s" << std::endl;
                } else if (key == mplot::key::r) {
                    this->stop();
                    this->vstate.set (state::campose_reset_request);
                }

            } else if (action == mplot::keyaction::release && !(mods & mplot::keymod::shift)) {

                if (key == mplot::key::w) {
                    this->move_state.reset (move_sense::forward);
                } else if (key == mplot::key::a && !mods) {
                    this->move_state.reset (move_sense::left);
                } else if (key == mplot::key::d) {
                    this->move_state.reset (move_sense::right);
                } else if (key == mplot::key::s) {
                    this->move_state.reset (move_sense::backward);
                } else if (key == mplot::key::p) {
                    this->move_state.reset (move_sense::up);
                } else if (key == mplot::key::l) {
                    this->move_state.reset (move_sense::down);
                } else if (key == mplot::key::up) {
                    this->move_state.reset (move_sense::rot_up);
                } else if (key == mplot::key::down) {
                    this->move_state.reset (move_sense::rot_down);
                } else if (key == mplot::key::left) {
                    this->move_state.reset (move_sense::rot_left);
                } else if (key == mplot::key::right) {
                    this->move_state.reset (move_sense::rot_right);
                } else if (key == mplot::key::comma) {
                    this->move_state.reset (move_sense::rot_roll_left);
                } else if (key == mplot::key::period) {
                    this->move_state.reset (move_sense::rot_roll_right);
                }
            }

            if (action == mplot::keyaction::press) {
                if (key == mplot::key::t) {
                    // Toggle the morph view
                    this->vstate.flip (state::show_cones);
                } else if (key == mplot::key::w && (mods & mplot::keymod::control)) {
                    // walk
                    std::cout << "Flip walk\n";
                    this->vstate.flip (state::walk);
                } else if (key == mplot::key::c) {
                    this->vstate.flip (state::show_camframe);
                } else if (key == mplot::key::o) {
                    std::cout << "Flip homing\n";
                    this->sim_opts.flip (craysim::options::homing_mode);
                } else if (key == mplot::key::escape) {
                    this->stop();

                } else if (key == mplot::key::f && this->vstate.test (state::paused)) {
                    this->vstate.set (state::stepfwd);

                } else if (key == mplot::key::space) {
                    this->vstate.flip (state::paused);

                } else if (key == mplot::key::page_up) {
                    int csamp = getCurrentEyeSamplesPerOmmatidium();
                    if (csamp < 32000) {
                        changeCurrentEyeSamplesPerOmmatidiumBy (csamp); // double
                    } else {
                        // else graphics memory use will get very large
                        std::cout << "max allowed samples\n";
                    }
                } else if (key == mplot::key::page_down) {
                    int csamp = getCurrentEyeSamplesPerOmmatidium();
                    changeCurrentEyeSamplesPerOmmatidiumBy (-(csamp/2)); // halve

                } else if (key == mplot::key::v) { // switch view
                    // cycle between:
                    this->switch_view_follows_mode();
                    // Don't show camframe when following
                    if (this->options.test (mplot::visual_options::viewFollowsVMBehind) == true) {
                        this->vstate.reset (state::show_camframe);
                    }
                }
            }
        }
    };

    // Add a suitable 2D projection to show our ant eye (distributed with OCES) in a flat fiew
    template <int glver>
    void add_ant_eye_spherical_projection (craysim::visual<glver>& v, mplot::compoundray::EyeVisual<glver>* eyevm2)
    {
        // First eye of eye pair (one spherical projection)
        std::uint32_t sz = 1024;
        float ps_rad = 0.0001f;                  // projection sphere radius
        sm::vec<> centre = { -0.00002f, 0, 0 };  // projection sphere centre

        if (v.oces_reader.read_success == true) {
            sz = v.oces_reader.position.size();
            ps_rad = 0.0002f;
            centre = { -0.00056, 0.00005, -0.00005 };
        }

        sm::mat<float, 4> twod_tr;                            // twod projection transformation
        float twod_scale = 1.0f;                              // twod projection scaling
        sm::vec<> twod_offset = { 0.0001f, 0.0f, 0.0f };      // twod projection translation to move to centre
        sm::vec<> twod_offset2 = { -0.0004f, 0.0007f, 0.0f }; // post scale/rotate translation
        sm::vec<> twod_shift = {0,0.0006,0};
        float rotn = -sm::mathconst<float>::pi_over_8;
        auto ptype = mplot::compoundray::EyeVisual<glver>::projection_type::mercator;
        if (v.oces_reader.read_success == true) {
            std::cout << "Read from oces file!!\n";
            ptype = mplot::compoundray::EyeVisual<glver>::projection_type::equirectangular;
            twod_tr.translate (twod_shift);
        } else {
            twod_tr.translate (twod_offset2);
            twod_tr.scale (twod_scale);
            twod_tr.rotate (sm::vec<>::uy(), rotn);
            twod_tr.translate (twod_offset);
        }

        // Projection sphere rotation about x axis by 0.2 radians. Numbers determined using oces_viewer
        sm::quaternion<float> psrotn (sm::vec<>::ux(), 0.2f);

        eyevm2->add_spherical_projection (ptype, twod_tr, centre, ps_rad, psrotn, 0, sz/2);

        // Second eye of the eye pair (another spherical projection)
        if (v.oces_reader.read_success == true) {
            if (v.oces_reader.mirrors.empty() == false) {
                centre = (v.oces_reader.mirrors[0] * centre).less_one_dim();
                sm::vec<> twod_shift_left = twod_shift;
                twod_shift_left[0] *= -1.0f;
                twod_tr.set_identity();
                twod_tr.translate (twod_shift_left);
                eyevm2->add_spherical_projection (ptype, twod_tr, centre, ps_rad, psrotn.invert(), sz/2, sz);
            }
        }
    }

} // namespace
