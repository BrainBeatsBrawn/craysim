module;

#include <cstdint>
#include <array>
#include <stdexcept>
#include <iostream>
#include <cmath>

export module craysim.doublehexgrid;

export import sm.vec;
export import sm.hexgrid;

import mplot.colourmap;

export import craysim.compoundray.ommatidia_data;

export namespace craysim
{
    enum class HexVisMode
    {
        Triangles, // Render triangles with a triangle vertex at the centre of each Hex. Fast (x3.7 cf. HexInterp).
        HexInterp  // Render each hex as an actual hex made of 6 triangles.
    };

    //! Display a grid that has two sections, each of which uses a single hexgrid, but has separate coordinates.
    template <std::int32_t glver = mplot::gl::version_4_1>
    struct doublehexgrid : public craysim::compoundray::ommatidia_data<glver>
    {
        // our hexgrid to visualize
        const sm::hexgrid<float>* hg;

        craysim::HexVisMode hexVisMode = craysim::HexVisMode::HexInterp;

        mplot::ColourMap<float> cm;

        // If true, show the flat representation of the hexgrid, ignoring some/all info in ommatidia
        bool show_flat = false;

        //! The length of the data structure that will be visualized. May be length of
        //! this->scalarData or of this->vectorData.
        std::uint32_t datasize = 0;

        doublehexgrid (const sm::hexgrid<float>* _hg, const sm::vec<float> _offset)
        {
            this->viewmatrix.translate (_offset);
            this->hg = _hg;
        }

        void reinitColours() // Originally coded as RGB only
        {
            if (this->scalarData == nullptr && this->ommData == nullptr) { return; }

            size_t n_verts = this->vertexColors.size(); // should be tube_vertices * n_omm
            if (n_verts == 0u) { return; } // model doesn't exist yet

            if (this->hexVisMode == craysim::HexVisMode::Triangles) {

                if (this->scalarData != nullptr) {
                    // Update from scalarData via scaling
                    //size_t n_omm = this->scalarData->size();
                    throw std::runtime_error ("doublehexgrid::reinitColours: Write logic for Triangle visMode and scalarData");

                } else { // this->ommData != nullptr
                    // Update from ommData direct
                    size_t n_omm = this->ommData->size();

                    if (n_verts < 3 * n_omm) {
                        throw std::runtime_error ("doublehexgrid: n_verts/n_omm sizes mismatch!");
                    }

                    for (size_t i = 0u; i < n_omm; ++i) {
                        // Update the 3 RGB values in vertexColors
                        std::array<float, 3> clr = this->cm.convert ((*this->ommData)[i][0]/255.0f, (*this->ommData)[i][1]/255.0f);
                        this->vertexColors[i*3] = clr[0];
                        this->vertexColors[i*3+1] = clr[1];
                        this->vertexColors[i*3+2] = clr[2];
                        //this->vertex_push (clr, this->vertexColors);
                    }
                }

            } else {
                if (this->scalarData != nullptr) {
                    // Update from scalarData via scaling
                    //size_t n_omm = this->scalarData->size();
                    throw std::runtime_error ("doublehexgrid::reinitColours: Write logic for HexInterp visMode and scalarData");

                } else { // this->ommData != nullptr
                    // Update from ommData direct
                    size_t n_omm = this->ommData->size();

                    if (n_verts < 3 * 7 * n_omm) {
                        std::stringstream ee;
                        ee << "doublehexgrid: n_verts["<<n_verts<<"] vs. n_omm["<<n_omm<<"] sizes mismatch (HexInterp)!";
                        throw std::runtime_error (ee.str());
                    }

                    for (size_t i = 0u; i < n_omm; ++i) {
                        // Update the 3 * 7 RGB values in vertexColors
                        std::array<float, 3> clr = this->setColour (i);
                        for (size_t j = 0u; j < 7; ++j) {

                            size_t base = i * (3 * 7) + j * 3;

                            // Hacky! FIXME when more awake
                            if (this->cm.getType() == mplot::ColourMapType::HSV) {
                                this->vertexColors[base] = clr[0];
                                this->vertexColors[base+1] = clr[1];
                                this->vertexColors[base+2] = clr[2];
                            } else {
                                // convert?
                                this->vertexColors[base] = (*this->ommData)[i][0];
                                this->vertexColors[base+1] = (*this->ommData)[i][1];
                                this->vertexColors[base+2] = (*this->ommData)[i][2];
                            }
                        }
                    }
                }
            }

            // Lastly, this call copies vertexColors (etc) into the OpenGL memory space
            this->reinit_colour_buffer();
        }

        //! Find datasize
        void determine_datasize()
        {
            this->datasize = 0;
            if (this->ommData != nullptr && !this->ommData->empty()) {
                this->datasize = this->ommData->size();
            } else if (this->scalarData != nullptr && !this->scalarData->empty()) {
                this->datasize = this->scalarData->size();
            } // else datasize remains 0
        }

        //! Do the computations to initialize the vertices that will represent the
        //! HexGrid.
        void initializeVertices()
        {
            this->idx = 0;
            this->determine_datasize();
            if (this->datasize == 0) {
                std::cout << "No data to show; return" << std::endl;
                return;
            }

            switch (this->hexVisMode) {
            case craysim::HexVisMode::Triangles:
            {
                this->initializeVerticesTris();
                break;
            }
            case craysim::HexVisMode::HexInterp:
            default:
            {
                this->initializeVerticesHexesInterpolated();
                break;
            }
            }
        }

        //! Colour setting
        std::array<float, 3> setColour (std::uint64_t ri)
        {
            std::array<float, 3> clr = { 0.0f, 0.0f, 0.0f };
            if (this->scalarData == nullptr) {
                if (this->ommData != nullptr) {
                    // Colour comes directly from ommData
                    clr = (*this->ommData)[ri];
                }
            } else {
                // Colour from scalarData
                clr = this->cm.convert ((*this->scalarData)[ri]);
            }
            return clr;
        }

        // Initialize vertex buffer objects and vertex array object.

        //! Initialize as triangled. Gives a smooth surface with much
        //! less compute than initializeVerticesHexesInterpolated.
        void initializeVerticesTris()
        {
            std::uint32_t nhex = this->hg->num();

            // doublehexgrid has two 'sections' of data
            for (std::uint32_t section = 0; section < 2; ++section) {
                std::uint32_t sdo = section * nhex; // section data offset
                for (std::uint32_t hi = 0; hi < nhex; ++hi) {
                    std::uint32_t dhi = hi + sdo;
                    std::array<float, 3> clr = this->setColour (hi);
                    // If dataCoords has been populated, use these for hex positions, allowing for
                    // mapping of the 2D HexGrid onto a 3D manifold.
                    if (this->ommatidia == nullptr || show_flat == true) {
                        this->vertex_push (this->hg->d_x[hi],
                                           this->hg->d_y[hi],
                                           0.0f, this->vertexPositions);

                    } else { // Otherwise use the positions directly in the HexGrid:
                        // positions from ommatidia
                        this->vertex_push ((*this->ommatidia)[dhi].relativePosition, this->vertexPositions);
                    }
                    this->vertex_push (clr, this->vertexColors);
                    this->vertex_push (0.0f, 0.0f, 1.0f, this->vertexNormals);
                }

                // Build indices based on neighbour relations in the HexGrid
                for (std::uint32_t hi = 0; hi < nhex; ++hi) {
                    if (this->hg->has_nne(hi) && this->hg->has_ne(hi)) {
                        //std::cout << "1st triangle " << hi << "->" << NNE(hi) << "->" << NE(hi) << std::endl;
                        this->indices.push_back (sdo + hi);
                        this->indices.push_back (sdo + this->hg->nne(hi));
                        this->indices.push_back (sdo + this->hg->ne(hi));
                    }

                    if (this->hg->has_nw(hi) && this->hg->has_nsw(hi)) {
                        //std::cout << "2nd triangle " << hi << "->" << NW(hi) << "->" << NSW(hi) << std::endl;
                        this->indices.push_back (sdo + hi);
                        this->indices.push_back (sdo + this->hg->nw(hi));
                        this->indices.push_back (sdo + this->hg->nsw(hi));
                    }
                }
                this->idx += nhex;
            }
        }

        //! Initialize as hexes, with z position of each of the 6
        //! outer edges of the hexes interpolated, but a single colour
        //! for each hex. Gives a smooth surface.
        void initializeVerticesHexesInterpolated()
        {
            this->computeHexes();
            // To show origin in model frame:
            //this->computeSphere (sm::vec<float>{0,0,0}, mplot::colour::navy, 0.003f);
        }

        // Compute vertices for the patchwork quilt of hexes
        void computeHexes()
        {
            if (this->hg == nullptr) {
                std::cerr << "Returning because hexgrid is a nullptr\n";
                return;
            }

            // Here's a complication. In a transformed grid, we can't rely on these. Should be able
            // to *compute* them though.
            float sr = this->hg->get_sr();
            float vne = this->hg->get_v_to_ne();
            float lr = this->hg->get_lr();

            std::uint32_t nhex = this->hg->num();

            // We have a double grid and use the hexgrid twice on the first half and second half.
            if (this->datasize != nhex * 2u) {
                //throw std::runtime_error ("datasize is not twice nhex");
                std::cerr << "Returning because datasize = " << datasize << " != 2 * nhex = " << (2 * nhex) << std::endl;
                return;
            }

            // x and y coords on the HexGrid. May be replaced if ommatidia has been set.
            float _x = 0.0f;
            float _y = 0.0f;
            // These Ts are all floats, right?
            float datumC = 0.0f;   // datum at the centre
            float datumNE = 0.0f;  // datum at the hex to the east.
            float datumNNE = 0.0f; // etc
            float datumNNW = 0.0f;
            float datumNW = 0.0f;
            float datumNSW = 0.0f;
            float datumNSE = 0.0f;

            float datum = 0.0f;
            float third = 0.3333333f;
            float half = 0.5f;
            sm::vec<float> vtx_0, vtx_1, vtx_2, vtx_3, vtx_4, vtx_5, vtx_6;

            sm::vec<float> coordC = { 0.0f, 0.0f, 0.0f };
            sm::vec<float> coordNE = coordC;
            sm::vec<float> coordNNE = coordC;
            sm::vec<float> coordNNW = coordC;
            sm::vec<float> coordNW = coordC;
            sm::vec<float> coordNSW = coordC;
            sm::vec<float> coordNSE = coordC;

            // Figure out an offset to centre the eyes about the current mv_offset. This
            // is the centroid of ommatidia
            sm::vec<float> coffs = {0,0,0};
#if 1
            if (this->ommatidia != nullptr) {
                // ommatidia is ptr to vector<Ommatidium>. The mean of relativePosition will work
                for (auto omm : *this->ommatidia) {
                    coffs -= omm.relativePosition;
                }
                coffs /= this->ommatidia->size();
            }
#endif
            for (std::uint32_t section = 0; section < 2; ++section) {
                std::uint32_t sdo = section * nhex; // section data offset
                for (std::uint32_t hi = 0; hi < nhex; ++hi) {

                    vtx_1.zero();
                    vtx_2.zero();

                    std::uint32_t dhi = hi + sdo;

                    if (this->ommatidia == nullptr || show_flat == true) {
                        _x = this->hg->d_x[hi];
                        _y = this->hg->d_y[hi];
                        // Use the linear scaled copy of the data, dcopy.
                        datumC   = 0.0f; // '_z'
                        datumNE  = datumC; // this->hg->has_ne(hi)  ? this->dcopy[this->hg->ne(hi)]  : datumC; // datum Neighbour East
                        datumNNE = datumC; // this->hg->has_nne(hi) ? this->dcopy[this->hg->nne(hi)] : datumC; // datum Neighbour North East
                        datumNNW = datumC; // this->hg->has_nnw(hi) ? this->dcopy[this->hg->nnw(hi)] : datumC; // etc
                        datumNW  = datumC; // this->hg->has_nw(hi)  ? this->dcopy[this->hg->nw(hi)]  : datumC;
                        datumNSW = datumC; // this->hg->has_nsw(hi) ? this->dcopy[this->hg->nsw(hi)] : datumC;
                        datumNSE = datumC; // this->hg->has_nse(hi) ? this->dcopy[this->hg->nse(hi)] : datumC;
                    } else {
                        // Get coordinates from ommatidia
                        coordC = coffs + (*this->ommatidia)[dhi].relativePosition;
                        _x = (*this->ommatidia)[dhi].relativePosition[0];
                        _y = (*this->ommatidia)[dhi].relativePosition[1];
                        datumC = (*this->ommatidia)[dhi].relativePosition[2];

                        coordNE  = this->hg->has_ne(hi)  ? coffs + (*this->ommatidia)[sdo + this->hg->ne(hi)].relativePosition  : coordC; // datum Neighbour East
                        coordNNE = this->hg->has_nne(hi) ? coffs + (*this->ommatidia)[sdo + this->hg->nne(hi)].relativePosition : coordC; // datum Neighbour North East
                        coordNNW = this->hg->has_nnw(hi) ? coffs + (*this->ommatidia)[sdo + this->hg->nnw(hi)].relativePosition : coordC; // etc
                        coordNW  = this->hg->has_nw(hi)  ? coffs + (*this->ommatidia)[sdo + this->hg->nw(hi)].relativePosition  : coordC;
                        coordNSW = this->hg->has_nsw(hi) ? coffs + (*this->ommatidia)[sdo + this->hg->nsw(hi)].relativePosition : coordC;
                        coordNSE = this->hg->has_nse(hi) ? coffs + (*this->ommatidia)[sdo + this->hg->nse(hi)].relativePosition : coordC;

                        datumNE = coordNE[2];
                        datumNNE = coordNNE[2];
                        datumNNW = coordNNW[2];
                        datumNW = coordNW[2];
                        datumNSW = coordNSW[2];
                        datumNSE = coordNSE[2];
                    }

                    // Use a single colour for each hex, even though hex z positions are
                    // interpolated. Do the _colour_ scaling:
                    std::array<float, 3> clr = this->setColour (hi);

                    // First push the 7 positions of the triangle vertices, starting with the centre

                    // Use the centre position as the first location for finding the normal vector
                    vtx_0 = (this->ommatidia == nullptr || show_flat == true) ? sm::vec<float>{ _x, _y, datumC } : coordC;
                    this->vertex_push (vtx_0, this->vertexPositions);

                    // The rotation from the transformation in the hexgrid (if any)
                    sm::mat<float, 3> lt = this->hg->tfm.linear().template as<float>();

                    // NE vertex
                    if (this->ommatidia == nullptr || show_flat == true) {
                        if (this->hg->has_nne(hi) && this->hg->has_ne(hi)) {
                            // Compute mean of this->data[hi] and NE and E hexes
                            datum = third * (datumC + datumNNE + datumNE);
                        } else if (this->hg->has_nne(hi) || this->hg->has_ne(hi)) {
                            if (this->hg->has_nne(hi)) {
                                datum = half * (datumC + datumNNE);
                            } else {
                                datum = half * (datumC + datumNE);
                            }
                        } else {
                            datum = datumC;
                        }
                        // Have to rotate after subtracting the center.
                        sm::vec<float> crnr = lt * sm::vec<float>{ sr, vne, 0 };
                        vtx_1 = crnr + sm::vec<float>{ _x, _y, datum };
                    } else {
                        // Similar logic, but for the coordinate, not just the data value
                        if (this->hg->has_nne(hi) && this->hg->has_ne(hi)) {
                            // Compute mean of coordC and NE and E hexes
                            vtx_1 = third * (coordC + coordNNE + coordNE);
                        } else if (this->hg->has_nne(hi) || this->hg->has_ne(hi)) {
                            if (this->hg->has_nne(hi)) {
                                vtx_1 = half * (coordC + coordNNE);
                            } else {
                                vtx_1 = half * (coordC + coordNE);
                            }
                        } else {
                            vtx_1 = coordC;
                        }
                    }
                    this->vertex_push (vtx_1, this->vertexPositions);

                    // SE vertex
                    if (this->ommatidia == nullptr || show_flat == true) {
                        if (this->hg->has_ne(hi) && this->hg->has_nse(hi)) {
                            datum = third * (datumC + datumNE + datumNSE);
                        } else if (this->hg->has_ne(hi) || this->hg->has_nse(hi)) {
                            if (this->hg->has_ne(hi)) {
                                datum = half * (datumC + datumNE);
                            } else {
                                datum = half * (datumC + datumNSE);
                            }
                        } else {
                            datum = datumC;
                        }
                        sm::vec<float> crnr = lt * sm::vec<float>{ sr, -vne, 0 };
                        vtx_2 = crnr + sm::vec<float>{ _x, _y, datum };
                    } else {
                        if (this->hg->has_ne(hi) && this->hg->has_nse(hi)) {
                            vtx_2 = third * (coordC + coordNE + coordNSE);
                        } else if (this->hg->has_ne(hi) || this->hg->has_nse(hi)) {
                            if (this->hg->has_ne(hi)) {
                                vtx_2 = half * (coordC + coordNE);
                            } else {
                                vtx_2 = half * (coordC + coordNSE);
                            }
                        } else {
                            vtx_2 = coordC;
                        }
                    }
                    this->vertex_push (vtx_2, this->vertexPositions);


                    // S
                    if (this->ommatidia == nullptr || show_flat == true) {
                        if (this->hg->has_nse(hi) && this->hg->has_nsw(hi)) {
                            datum = third * (datumC + datumNSE + datumNSW);
                        } else if (this->hg->has_nse(hi) || this->hg->has_nsw(hi)) {
                            if (this->hg->has_nse(hi)) {
                                datum = half * (datumC + datumNSE);
                            } else {
                                datum = half * (datumC + datumNSW);
                            }
                        } else {
                            datum = datumC;
                        }
                        sm::vec<float> crnr = lt * sm::vec<float>{ 0, -lr, 0 };
                        vtx_3 = crnr + sm::vec<float>{ _x, _y, datum };
                    } else {
                        if (this->hg->has_nse(hi) && this->hg->has_nsw(hi)) {
                            vtx_3 = third * (coordC + coordNSE + coordNSW);
                        } else if (this->hg->has_nse(hi) || this->hg->has_nsw(hi)) {
                            if (this->hg->has_nse(hi)) {
                                vtx_3 = half * (coordC + coordNSE);
                            } else {
                                vtx_3 = half * (coordC + coordNSW);
                            }
                        } else {
                            vtx_3 = coordC;
                        }
                    }
                    this->vertex_push (vtx_3, this->vertexPositions);

                    // SW
                    if (this->ommatidia == nullptr || show_flat == true) {
                        if (this->hg->has_nw(hi) && this->hg->has_nsw(hi)) {
                            datum = third * (datumC + datumNW + datumNSW);
                        } else if (this->hg->has_nw(hi) || this->hg->has_nsw(hi)) {
                            if (this->hg->has_nw(hi)) {
                                datum = half * (datumC + datumNW);
                            } else {
                                datum = half * (datumC + datumNSW);
                            }
                        } else {
                            datum = datumC;
                        }
                        sm::vec<float> crnr = lt * sm::vec<float>{ -sr, -vne, 0 };
                        vtx_4 = crnr + sm::vec<float>{ _x, _y, datum };
                    } else {
                        if (this->hg->has_nw(hi) && this->hg->has_nsw(hi)) {
                            vtx_4 = third * (coordC + coordNW + coordNSW);
                        } else if (this->hg->has_nw(hi) || this->hg->has_nsw(hi)) {
                            if (this->hg->has_nw(hi)) {
                                vtx_4 = half * (coordC + coordNW);
                            } else {
                                vtx_4 = half * (coordC + coordNSW);
                            }
                        } else {
                            vtx_4 = coordC;
                        }
                    }
                    this->vertex_push (vtx_4, this->vertexPositions);

                    // NW
                    if (this->ommatidia == nullptr || show_flat == true) {
                        if (this->hg->has_nnw(hi) && this->hg->has_nw(hi)) {
                            datum = third * (datumC + datumNNW + datumNW);
                        } else if (this->hg->has_nnw(hi) || this->hg->has_nw(hi)) {
                            if (this->hg->has_nnw(hi)) {
                                datum = half * (datumC + datumNNW);
                            } else {
                                datum = half * (datumC + datumNW);
                            }
                        } else {
                            datum = datumC;
                        }
                        sm::vec<float> crnr = lt * sm::vec<float>{ -sr, vne, 0 };
                        vtx_5 = crnr + sm::vec<float>{ _x, _y, datum };
                    } else {
                        if (this->hg->has_nnw(hi) && this->hg->has_nw(hi)) {
                            vtx_5 = third * (coordC + coordNNW + coordNW);
                        } else if (this->hg->has_nnw(hi) || this->hg->has_nw(hi)) {
                            if (this->hg->has_nnw(hi)) {
                                vtx_5 = half * (coordC + coordNNW);
                            } else {
                                vtx_5 = half * (coordC + coordNW);
                            }
                        } else {
                            vtx_5 = coordC;
                        }
                    }
                    this->vertex_push (vtx_5, this->vertexPositions);

                    // N
                    if (this->ommatidia == nullptr || show_flat == true) {
                        if (this->hg->has_nnw(hi) && this->hg->has_nne(hi)) {
                            datum = third * (datumC + datumNNW + datumNNE);
                        } else if (this->hg->has_nnw(hi) || this->hg->has_nne(hi)) {
                            if (this->hg->has_nnw(hi)) {
                                datum = half * (datumC + datumNNW);
                            } else {
                                datum = half * (datumC + datumNNE);
                            }
                        } else {
                            datum = datumC;
                        }
                        sm::vec<float> crnr = lt * sm::vec<float>{ 0, lr, 0 };
                        vtx_6 = crnr + sm::vec<float>{ _x, _y, datum };
                    } else {
                        if (this->hg->has_nnw(hi) && this->hg->has_nne(hi)) {
                            vtx_6 = third * (coordC + coordNNW + coordNNE);
                        } else if (this->hg->has_nnw(hi) || this->hg->has_nne(hi)) {
                            if (this->hg->has_nnw(hi)) {
                                vtx_6 = half * (coordC + coordNNW);
                            } else {
                                vtx_6 = half * (coordC + coordNNE);
                            }
                        } else {
                            vtx_6 = coordC;
                        }
                    }
                    this->vertex_push (vtx_6, this->vertexPositions);

                    // From vtx_0, and any two of vtx_1 to vtx_6, compute two planes and thus the normal vector.
                    sm::vec<float> plane1 = {0,0,0};
                    sm::vec<float> plane2 = {0,0,0};

                    // First get the first plane
                    std::int32_t plane1_vtx = -1;
                    if ((vtx_1 - vtx_0).length() > 0.0f) {
                        plane1 = vtx_1 - vtx_0;
                        plane1_vtx = 1;
                    } else if ((vtx_2 - vtx_0).length() > 0.0f) {
                        plane1 = vtx_2 - vtx_0;
                        plane1_vtx = 2;
                    } else if ((vtx_3 - vtx_0).length() > 0.0f) {
                        plane1 = vtx_3 - vtx_0;
                        plane1_vtx = 3;
                    } else if ((vtx_4 - vtx_0).length() > 0.0f) {
                        plane1 = vtx_4 - vtx_0;
                        plane1_vtx = 4;
                    } else if ((vtx_5 - vtx_0).length() > 0.0f) {
                        plane1 = vtx_5 - vtx_0;
                        plane1_vtx = 5;
                    } else if ((vtx_6 - vtx_0).length() > 0.0f) {
                        plane1 = vtx_6 - vtx_0;
                        plane1_vtx = 6;
                    } else {
                        throw std::runtime_error ("doublehexgrid: vtx_0 has no neighbour?!");
                    }

                    // Now select a second plane
                    if (plane1_vtx != 1 && (vtx_1 - vtx_0).length() > 0.0f) {
                        plane2 = vtx_1 - vtx_0;
                    } else if (plane1_vtx != 2 && (vtx_2 - vtx_0).length() > 0.0f) {
                        plane2 = vtx_2 - vtx_0;
                    } else if (plane1_vtx != 3 && (vtx_3 - vtx_0).length() > 0.0f) {
                        plane2 = vtx_3 - vtx_0;
                    } else if (plane1_vtx != 4 && (vtx_4 - vtx_0).length() > 0.0f) {
                        plane2 = vtx_4 - vtx_0;
                    } else if (plane1_vtx != 5 && (vtx_5 - vtx_0).length() > 0.0f) {
                        plane2 = vtx_5 - vtx_0;
                    } else if (plane1_vtx != 6 && (vtx_6 - vtx_0).length() > 0.0f) {
                        plane2 = vtx_6 - vtx_0;
                    } else {
                        throw std::runtime_error ("Can't do planes with only 1 neighbour to a hex"); // or gracefully handle? Can select a random vector!
                    }

                    // The normal is the cross product of the planes.
                    sm::vec<float> vnorm = plane2.cross (plane1);
                    vnorm.renormalize();

                    this->vertex_push (vnorm, this->vertexNormals);
                    this->vertex_push (vnorm, this->vertexNormals);
                    this->vertex_push (vnorm, this->vertexNormals);
                    this->vertex_push (vnorm, this->vertexNormals);
                    this->vertex_push (vnorm, this->vertexNormals);
                    this->vertex_push (vnorm, this->vertexNormals);
                    this->vertex_push (vnorm, this->vertexNormals);

                    // Seven vertices with the same colour
                    this->vertex_push (clr, this->vertexColors);
                    this->vertex_push (clr, this->vertexColors);
                    this->vertex_push (clr, this->vertexColors);
                    this->vertex_push (clr, this->vertexColors);
                    this->vertex_push (clr, this->vertexColors);
                    this->vertex_push (clr, this->vertexColors);
                    this->vertex_push (clr, this->vertexColors);

                    // Define indices now to produce the 6 triangles in the hex
                    this->indices.push_back (this->idx+1);
                    this->indices.push_back (this->idx);
                    this->indices.push_back (this->idx+2);

                    this->indices.push_back (this->idx+2);
                    this->indices.push_back (this->idx);
                    this->indices.push_back (this->idx+3);

                    this->indices.push_back (this->idx+3);
                    this->indices.push_back (this->idx);
                    this->indices.push_back (this->idx+4);

                    this->indices.push_back (this->idx+4);
                    this->indices.push_back (this->idx);
                    this->indices.push_back (this->idx+5);

                    this->indices.push_back (this->idx+5);
                    this->indices.push_back (this->idx);
                    this->indices.push_back (this->idx+6);

                    this->indices.push_back (this->idx+6);
                    this->indices.push_back (this->idx);
                    this->indices.push_back (this->idx+1);

                    this->idx += 7; // 7 vertices (each of 3 floats for x/y/z), 18 indices.
                }
            } // section
        }
    };

} // namespace mplot
