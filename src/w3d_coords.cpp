#include "w3d_coords.h"

#include <godot_cpp/core/class_db.hpp>
#include <cmath>

using namespace godot;

namespace world3d
{

	W3DCoords::W3DCoords()
	{
	}

	W3DCoords::~W3DCoords()
	{
	}

	void W3DCoords::_bind_methods()
	{
		ClassDB::bind_static_method("W3DCoords", D_METHOD("geodetic_to_ecef", "latitude", "longitude", "height"), &W3DCoords::geodetic_to_ecef);
		ClassDB::bind_static_method("W3DCoords", D_METHOD("ecef_to_geodetic", "ecef"), &W3DCoords::ecef_to_geodetic);
		ClassDB::bind_static_method("W3DCoords", D_METHOD("geodetic_normal", "latitude", "longitude"), &W3DCoords::geodetic_normal);
		ClassDB::bind_static_method("W3DCoords", D_METHOD("enu_to_ecef_transform", "latitude", "longitude", "height", "x_axis_west"), &W3DCoords::enu_to_ecef_transform, DEFVAL(false));
	}

	static inline double deg2rad(double d) { return d * (Math_PI / 180.0); }
	static inline double rad2deg(double r) { return r * (180.0 / Math_PI); }

	Vector3 W3DCoords::geodetic_to_ecef(double latitude, double longitude, double height)
	{
		const double lat = deg2rad(latitude);
		const double lon = deg2rad(longitude);
		const double sin_lat = std::sin(lat);
		const double cos_lat = std::cos(lat);
		const double sin_lon = std::sin(lon);
		const double cos_lon = std::cos(lon);

		// Prime vertical radius of curvature.
		const double N = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);

		const double x = (N + height) * cos_lat * cos_lon;
		const double y = (N + height) * cos_lat * sin_lon;
		const double z = (N * (1.0 - WGS84_E2) + height) * sin_lat;

		return Vector3(x, y, z);
	}

	Vector3 W3DCoords::ecef_to_geodetic(Vector3 ecef)
	{
		const double x = ecef.x;
		const double y = ecef.y;
		const double z = ecef.z;

		const double lon = std::atan2(y, x);
		const double p = std::sqrt(x * x + y * y);

		// Bowring's iterative method (converges in a few iterations).
		double lat = std::atan2(z, p * (1.0 - WGS84_E2));
		double height = 0.0;
		for (int i = 0; i < 5; ++i)
		{
			const double sin_lat = std::sin(lat);
			const double N = WGS84_A / std::sqrt(1.0 - WGS84_E2 * sin_lat * sin_lat);
			height = p / std::cos(lat) - N;
			lat = std::atan2(z, p * (1.0 - WGS84_E2 * N / (N + height)));
		}

		return Vector3(rad2deg(lat), rad2deg(lon), height);
	}

	Vector3 W3DCoords::geodetic_normal(double latitude, double longitude)
	{
		const double lat = deg2rad(latitude);
		const double lon = deg2rad(longitude);
		const double cos_lat = std::cos(lat);
		return Vector3(cos_lat * std::cos(lon), cos_lat * std::sin(lon), std::sin(lat)).normalized();
	}

	Transform3D W3DCoords::enu_to_ecef_transform(double latitude, double longitude, double height, bool x_axis_west)
	{
		const double lat = deg2rad(latitude);
		const double lon = deg2rad(longitude);
		const double sin_lat = std::sin(lat);
		const double cos_lat = std::cos(lat);
		const double sin_lon = std::sin(lon);
		const double cos_lon = std::cos(lon);

		// Geographic basis vectors expressed in ECEF (E, N, U is right-handed).
		const Vector3 east(-sin_lon, cos_lon, 0.0);
		const Vector3 north(-sin_lat * cos_lon, -sin_lat * sin_lon, cos_lat);
		const Vector3 up(cos_lat * cos_lon, cos_lat * sin_lon, sin_lat);

		const Vector3 origin = geodetic_to_ecef(latitude, longitude, height);

		// Build a proper right-handed (det +1) Y-up basis. The columns are the
		// images of the local (X, Y, Z) axes in ECEF.
		Basis basis;
		if (x_axis_west)
		{
			// +X=West, +Y=Up, +Z=North (scenery3d LV95 convention). West x Up = North.
			basis.set_column(0, -east);
			basis.set_column(1, up);
			basis.set_column(2, north);
		}
		else
		{
			// +X=East, +Y=Up, +Z=South. East x Up = -North = South.
			basis.set_column(0, east);
			basis.set_column(1, up);
			basis.set_column(2, -north);
		}
		return Transform3D(basis, origin);
	}

}
