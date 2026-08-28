/*
 * A sm::vec version of compound-ray's Ommatidium
 */
export module craysim.compoundray.ommatidium;

export import sm.vec;

export namespace craysim::compoundray
{
    // This is a binary-compatible equivalent to struct Ommatidium from cameras/CompoundEyeDataTypes.h in compound-ray.
    // Use reinterpret_cast<std::vector<craysim::compoundray::Ommatidium>*>(ommatidia) if your ommatidia originate inside compound ray.
    struct Ommatidium
    {
        sm::vec<float, 3> relativePosition = {};
        sm::vec<float, 3> relativeDirection = {};
        float acceptanceAngleRadians = 0.0f;
        float focalPointOffset = 0.0f;
    };
}
