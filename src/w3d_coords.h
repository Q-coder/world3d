#ifndef W3D_COORDS_H
#define W3D_COORDS_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/transform3d.hpp>

namespace world3d
{

	// Converts between WGS84 geodetic coordinates (latitude/longitude/height) and
	// Earth-Centred Earth-Fixed (ECEF) metric coordinates, and builds the local
	// East-North-Up (ENU) tangent frame used to mount a flat local patch (e.g. the
	// scenery3d Swiss LV95 tiles) onto the curved global shell.
	//
	// The global shell is rendered directly in ECEF, so all positions are in the
	// ~6.4 million metre range. This requires the double-precision Godot build.
	class W3DCoords : public godot::RefCounted
	{
		GDCLASS(W3DCoords, godot::RefCounted);

	public:
		// WGS84 ellipsoid constants.
		static constexpr double WGS84_A = 6378137.0;             // semi-major axis (m)
		static constexpr double WGS84_F = 1.0 / 298.257223563;   // flattening
		static constexpr double WGS84_B = WGS84_A * (1.0 - WGS84_F); // semi-minor axis (m)
		// First eccentricity squared: e^2 = 2f - f^2.
		static constexpr double WGS84_E2 = WGS84_F * (2.0 - WGS84_F);

	protected:
		static void _bind_methods();

	public:
		W3DCoords();
		~W3DCoords();

		// --- Geodetic <-> ECEF ---
		// Latitude/longitude in degrees, height in metres above the ellipsoid.
		// Returns ECEF (X, Y, Z) in metres.
		static godot::Vector3 geodetic_to_ecef(double latitude, double longitude, double height);

		// ECEF (metres) -> (latitude_deg, longitude_deg, height_m).
		static godot::Vector3 ecef_to_geodetic(godot::Vector3 ecef);

		// Unit sphere direction (normalized geocentric-ish) for a geodetic lat/lon,
		// i.e. the outward ellipsoid surface normal at that point.
		static godot::Vector3 geodetic_normal(double latitude, double longitude);

		// --- Local tangent frame (ENU-family) ---
		// Builds the transform mapping a flat local Y-up frame at the given geodetic
		// point into ECEF. Use it as the Transform3D of a child node hosting a flat
		// local tile system (e.g. the scenery3d Swiss LV95 tiles).
		//
		// A geographic (East, North, Up) triad is right-handed in the order E,N,U,
		// so assigning (East, Up, North) directly to a Y-up (X,Y,Z) frame would be
		// left-handed and mirror any mounted geometry. To stay a proper rotation we
		// therefore offer two right-handed conventions:
		//   x_axis_west = false -> +X=East,  +Y=Up, +Z=South (North = -Z)
		//   x_axis_west = true  -> +X=West,  +Y=Up, +Z=North   (scenery3d LV95 convention)
		// Both have determinant +1, so triangle winding is preserved.
		static godot::Transform3D enu_to_ecef_transform(double latitude, double longitude, double height, bool x_axis_west = false);
	};

}

#endif // W3D_COORDS_H
