/// ----------------------------------------------------------------------------
/// MATH/UTILS LIBRARY

internal timespec time_now()
{
	timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now;
}

internal float time_elapsed_milliseconds(timespec start)
{
	// Todo(Leo): decide whether or not this provides enough precision
	timespec now 		= time_now();
	double seconds 		= difftime(now.tv_sec, start.tv_sec);
	long nanoseconds 	= now.tv_nsec - start.tv_nsec;

	float milliseconds 	= seconds * 1000 + nanoseconds / 1'000'000.0;

	return milliseconds;
}

internal float time_elapsed_seconds(timespec start)
{
	timespec now 		= time_now();
	double seconds 		= difftime(now.tv_sec, start.tv_sec);
	long nanoseconds 	= now.tv_nsec - start.tv_nsec;

	// Todo(Leo): maybe first do long divisions...
	float result 		= (float)seconds + nanoseconds / 1'000'000'000.0;
	return result;
}

internal void log_info(char const * message)
{
	__android_log_write(ANDROID_LOG_INFO, "Game", message);
}

internal void log_error(char const * message)
{
	__android_log_write(ANDROID_LOG_ERROR, "Game", message);
}

internal float float_clamp(float value, float min, float max)
{
	if (value < min)
		value = min;
	else if (value > max)
		value = max;
	return value;
}

internal float float_lerp(float a, float b, float t)
{
	a = (1 - t) * a + t * b;
	return a;
}

struct v2
{
	float x, y;
};

internal v2 operator + (v2 a, v2 b)
{
	v2 result = {a.x + b.x, a.y + b.y};
	return result;
}

internal v2 operator - (v2 a, v2 b)
{
	v2 result = {a.x - b.x, a.y - b.y};
	return result;
}

internal v2 operator * (v2 v, float f)
{
	v.x *= f;
	v.y *= f;
	return v;
}

internal v2 operator / (v2 v, float f)
{
	v.x /= f;
	v.y /= f;
	return v;
}

internal float v2_dot(v2 a, v2 b)
{
	float dot = a.x * b.x + a.y * b.y;
	return dot;
}

internal float v2_magnitude(v2 v)
{
	float result = std::sqrt(v.x * v.x + v.y * v.y);
	return result;
}

internal v2 v2_normalize(v2 v)
{
	float magnitude = v2_magnitude(v);

	v.x /= magnitude;
	v.y /= magnitude;

	return v;
}

internal v2 v2_lerp(v2 a, v2 b, float t)
{
	a.x += (b.x - a.x) * t;
	a.y += (b.y - a.y) * t;
	return a;
}

internal v2 v2_bezier_lerp(v2 a, v2 b, v2 c, float t)
{
	v2 ab 	= v2_lerp(a,b,t);
	v2 bc 	= v2_lerp(b,c,t);
	v2 abc 	= v2_lerp(ab, bc, t);
	return abc;
}

internal v2 v2_cubic_bezier_lerp(v2 a, v2 b, v2 c, v2 d, float t)
{
	v2 ab = v2_lerp(a,b,t);
	v2 bc = v2_lerp(b,c,t);
	v2 cd = v2_lerp(c,d,t);

	v2 abc = v2_lerp(ab, bc, t);
	v2 bcd = v2_lerp(bc, cd, t);

	v2 abcd = v2_lerp(abc, bcd, t);

	return abcd;
}

struct v3
{
	float r, g, b;
};

struct v3_hsv
{
	float h, s, v;
};

internal float v3_min_component(v3 v)
{
	float min = v.r;
	if (v.g < min)
		min = v.g;
	if (v.b < min)
		min = v.b;
	
	return min;
}

internal float v3_max_component(v3 v)
{
	float max = v.r;
	if (v.g > max)
		max = v.g;
	if (v.b > max)
		max = v.b;

	return max;
}

internal v3 v3_lerp(v3 a, v3 b, float t)
{
	float omt = 1 - t;

	a.r = omt * a.r + t * b.r;
	a.g = omt * a.g + t * b.g;
	a.b = omt * a.b + t * b.b;

	return a;
}

v3_hsv hsv_from_rgb(v3 rgb)
{
	float min = v3_min_component(rgb);
	float max = v3_max_component(rgb);
	float delta = max- min;

	float hue;
	if (max == rgb.r)
	{
		hue = std::fmodf((rgb.g - rgb.b) /delta, 6);
	}
	else if (max == rgb.g)
	{
		hue = (rgb.b - rgb.r) / delta + 2; 
	}
	else if (max == rgb.b)
	{
		hue = (rgb.r - rgb.g) / delta + 4;
	}

	float saturation;

	if (max > 0)
	{
		saturation = delta / max;
	}
	else
	{
		saturation = 0;
	}

	float value = max;

	return {hue, saturation, value};
}

internal v3_hsv v3_hsv_lerp(v3_hsv a, v3_hsv b, float t)
{
	float hueDeltaForward = b.h - a.h;
	float hueDeltaBackward = a.h - b.h;

	float hue;

	if (std::abs(hueDeltaForward) < std::abs(hueDeltaBackward))
	{
		float interpolatedHueDelta = float_lerp(a.h, b.h, t);
		hue = a.h + interpolatedHueDelta;
	}
	else
	{
		float interpolatedHueDelta = float_lerp(b.h, a.h, t);
		hue = a.h + interpolatedHueDelta;
	}

	hue = std::fmodf(hue, 6.0);

	float saturation = float_lerp(a.s, b.s, t);
	float value = float_lerp(a.v, b.v, t);

	v3_hsv result = {hue, saturation, value};
	return result;
}

internal v3 rgb_from_hsv(v3_hsv hsv)
{
	float r,g,b;

	float c = hsv.s * hsv.v;
	float x = c * (1 - std::abs(std::fmodf(hsv.h, 2) - 1));
	float m = hsv.v - c;

	if (hsv.h < 1)
	{
		r = c;
		g = x;
		b = 0;
	}
	else if(hsv.h < 2)
	{
		r = x;
		g = c;
		b = 0;
	}
	else if(hsv.h < 3)
	{	
		r = 0;
		g = c;
		b = x;
	}
	else if(hsv.h < 4)
	{
		r = 0;
		g = x;
		b = c;
	}
	else if(hsv.h < 5)
	{
		r = x;
		g = 0;
		b = c;
	}
	else if(hsv.h < 6)
	{
		r = c;
		g = 0;
		b = x;
	}
	else
	{
		/// ERRROR
		r = 1;
		g = 0;
		b = 1;
	}

	return {r + m,g + m,b + m} ;
}

struct v4
{
	float r, g, b, t;
};

v3 rgb(v4 v)
{
	return {v.r, v.g, v.b};
}