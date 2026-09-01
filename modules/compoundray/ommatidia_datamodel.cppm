/*
 * Ommatidia data layer class. Similar to VisualDataModel, but for Ommatidia data.
 */

module;

#include <cstdint>
#include <iostream>
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <cmath>
#include <map>
#include <set>

export module craysim.compoundray.ommatidia_datamodel;

export import sm.mathconst;
export import sm.vec;

export import mplot.gl.version;
export import mplot.visualmodel;
import mplot.tools;

export import craysim.compoundray.ommatidium;

export namespace craysim::compoundray
{
    /**
     * A datamodel layer class for VisualModels that visualize the data format used in
     * compound-ray. VisualModels that extend this class have access to: a) a pointer to a vector of
     * Ommatidium data types that give access to the position and orientation of each ommatidium b)
     * pointers to vector and scalar data values for each ommatidium.
     *
     * Compare this class with mplot::VisualDataModel, which does a similar, though more generic
     * job.
     */
    template <std::int32_t glver>
    struct ommatidia_datamodel : public mplot::VisualModel<glver>
    {
        // We require a reinitColours function in derived types
        virtual void reinitColours() = 0;
        // The colours detected by each ommatidium
        std::vector<std::array<float, 3>>* ommData = nullptr;
        // If we are instead rendering scalar data, use scalarData
        std::vector<float>* scalarData = nullptr;
        // The position and orientation of each ommatidium
        std::vector<craysim::compoundray::Ommatidium>* ommatidia = nullptr;
        // An optional head mesh
        const mplot::meshgroup* head_mesh = nullptr;
    };

    // Helper function. Read the compound-ray csv eye file into ommatidia. ommatidia should be a pointer to an allocate vector.
    [[maybe_unused]] std::vector<craysim::compoundray::Ommatidium>*
    readEye (std::vector<craysim::compoundray::Ommatidium>* ommatidia, const std::string& path)
    {
        if (ommatidia == nullptr) { return ommatidia; }

        std::cout << "Path: " << path << std::endl;

        ommatidia->clear();

        std::ifstream eyeDataFile (path, std::ifstream::in);
        if(!eyeDataFile.is_open()) {
            std::cout << "Failed to open eye data file " << path << "\n";
            return ommatidia;
        }

        std::string line;
        size_t ommCount = 0;
        while (std::getline (eyeDataFile, line)) {
            std::vector<std::string> splitData = mplot::tools::stringToVector (line, " ");
            if (splitData.size() < 8) {
                std::cout << "Malformed line, continue...\n";
                continue;
            }
            craysim::compoundray::Ommatidium o = {
                sm::vec<float, 3>{ std::stof(splitData[0]), std::stof(splitData[1]), std::stof(splitData[2]) },
                sm::vec<float, 3>{ std::stof(splitData[3]), std::stof(splitData[4]), std::stof(splitData[5]) },
                std::stof(splitData[6]),
                std::stof(splitData[7])
            };
            std::cout << "o: " << o.relativePosition << "; " << o.relativeDirection << "; " << o.acceptanceAngleRadians << "; " << o.focalPointOffset << std::endl;
            ommatidia->push_back (o);
            ommCount++;
        }
        std::cout <<  "  Loaded " << ommCount << " ommatidia." << std::endl;

        return ommatidia;
    }
}
