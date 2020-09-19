/*
Leo Tamminen

Resources:
	https://github.com/android/ndk-samples/blob/master/native-activity/app/src/main/cpp/main.cpp
	https://community.arm.com/developer/tools-software/oss-platforms/b/android-blog/posts/check-your-context-if-glcreateshader-returns-0-and-gl_5f00_invalid_5f00_operation

	This says saving file to internal storage requires no permissions
	https://developer.android.com/training/data-storage

	Asset accessing hints, although this was overly verbose
	https://en.wikibooks.org/wiki/OpenGL_Programming/Android_GLUT_Wrapper#Accessing_assets

Done list of features:
	- draw using finger
	- drawing faster or slower produces different color
	- holding finger still for a moment before drawing produces a gradually wider line
	- erase after a double tap

Todo list of features:
	- smooth(er) line tangents derived from previous sections
	- animate trashing the texture

	- bigger canvas and zoom (default view to max zoom out), maybe translate moving two fingers to same direction
	- draw line between 2 fingers (how, are we not using 2 fingers to zoom)
		-> this works now by first adding 1 finger, then second and finally releasing the first.
			program thinks that user moved their finger so far in one frame

Things what this thing is and what it is not:
	- It is NOT an image authoring tool
	- Is is an interactive toy or a game-like thing where user produces images
	- There should be no pressure or fear of empty canvas as this does not allow
		for particular use of drawing tools


Coordinates Memo:
	- Android screen coordinates go from [0,0] in top left corner to [maxX, maxY]
	- OpenGl normalized device coordinates go from [-1,-1] in bottom left corner to [1, 1]
	- Game coordinates use android model, because this is simple enough
	- Transform before rendering

Font:
	Barlow Condensed - Google Fonts

Todo "levels":
	use noise on stroke width, and to punch holes

	one level has subtle noised width with single color, that decays over time
	one level has same gradient as now, but noisely leaves gaps
*/

// ANDROID things
#include <android/log.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <jni.h>

// POSIX things
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>

#include <sys/stat.h>
#include <sys/resource.h>

// C standard things
#include <time.h>
#include <cmath>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// Todo(Leo): Remove asserts, and just provide fallback behaviour or something. The do not work so nicely on android development
#include <cassert>

#include <type_traits>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// TODO(Leo): Make these also our own
/* For debug builds, always enable the debug traces in this library */
#ifndef NDEBUG
#  define GLUE_LOGV(...)  ((void)__android_log_print(ANDROID_LOG_VERBOSE, "threaded_app", __VA_ARGS__))
#else
#  define GLUE_LOGV(...)  ((void)0)
#endif

#define internal static

// Todo(Leo): also use these...
using bool32 	= __int32_t;
using int32 	= __int32_t;
using uint8 	= __uint8_t;
using uint32 	= __uint32_t;

/// ----------------------------------------------------------------------------
/// MATH/UTILS LIBRARY

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

/// ------------------------------------------------------------------------
/// GAME RELATED THINGS

#include <EGL/egl.h>
#include <GLES3/gl3.h>

// Todo(Leo): define these away in release build
const char * gl_error_string(GLenum error)
{
	switch(error)
	{
		case GL_NO_ERROR:						return "GL_NO_ERROR";
		case GL_INVALID_ENUM:					return "GL_INVALID_ENUM ";
		case GL_INVALID_VALUE:					return "GL_INVALID_VALUE";
		case GL_INVALID_OPERATION:				return "GL_INVALID_OPERATION";
		case GL_INVALID_FRAMEBUFFER_OPERATION: 	return "GL_INVALID_FRAMEBUFFER_OPERATION";
		case GL_OUT_OF_MEMORY:					return "GL_OUT_OF_MEMORY";

		default:
			return "Unsupported Error";
	}
}

char const * gl_framebuffer_status_string(GLenum status)
{
	switch (status)
	{
		case GL_FRAMEBUFFER_COMPLETE: 						return "GL_FRAMEBUFFER_COMPLETE";
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:			return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
		case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:			return "GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS";
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:	return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
		case GL_FRAMEBUFFER_UNSUPPORTED:					return "GL_FRAMEBUFFER_UNSUPPORTED";

		default:
			return "Unknown framebuffer status";
	}
}

struct GLContext
{
	bool32 isGood;

	EGLDisplay display;

	EGLSurface surface;
	EGLContext eglContext;

	int32 width;
	int32 height;
	float ratio () { return (float)width / height; }
};

internal GLContext initialize_opengl(ANativeWindow * window)
{
	GLContext context;

	log_info("Start initialize opengl");

	context.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(context.display, nullptr, nullptr);

	const EGLint attributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_CONFORMANT, EGL_OPENGL_ES3_BIT,
		EGL_NONE
	};
	EGLint configCount;
	eglChooseConfig(context.display, attributes, nullptr, 0, &configCount);
	assert(configCount >= 0);
	assert(configCount < 100);
	EGLConfig supportedConfigs [100];
	eglChooseConfig(context.display, attributes, supportedConfigs, configCount, &configCount);

	/* Note(Leo): We do this to select a configuration we would prefer,
	but it is not strictly necessary */
	int selectedConfigIndex = 0;
	for (int configIndex = 0; configIndex < configCount; ++configIndex)
	{
		auto & config = supportedConfigs[configIndex];
		EGLint
			redSize,
			greenSize,
			blueSize,
			depthSize;

		if (eglGetConfigAttrib(context.display, config, EGL_RED_SIZE, &redSize) &&
			eglGetConfigAttrib(context.display, config, EGL_GREEN_SIZE, &greenSize) &&
			eglGetConfigAttrib(context.display, config, EGL_BLUE_SIZE, &blueSize) &&
			eglGetConfigAttrib(context.display, config, EGL_DEPTH_SIZE, &depthSize) &&
			redSize == 8 && greenSize == 8 && blueSize == 8 && depthSize == 0)
		{
			log_info("Configuration found");
			selectedConfigIndex = configIndex;
			break;
		}
	}

	EGLConfig selectedConfig = supportedConfigs[selectedConfigIndex];

	EGLint format;
	eglGetConfigAttrib(context.display, selectedConfig, EGL_NATIVE_VISUAL_ID, &format);

	context.surface = eglCreateWindowSurface(context.display, selectedConfig, window, nullptr);

	EGLint contextAttributes [] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,  
		EGL_NONE
	};
	context.eglContext = eglCreateContext(context.display, selectedConfig, EGL_NO_CONTEXT, contextAttributes);

	if (eglMakeCurrent(context.display, context.surface, context.surface, context.eglContext) == EGL_FALSE)
	{
		log_error("Failed to make egl stuff current");
		return GLContext{};
	}

	context.isGood = true;

	eglQuerySurface(context.display, context.surface, EGL_WIDTH, &context.width);
	eglQuerySurface(context.display, context.surface, EGL_HEIGHT, &context.height);

	__android_log_print(ANDROID_LOG_INFO, "Game", "OpenGL vendor: %s", glGetString(GL_VENDOR));
	__android_log_print(ANDROID_LOG_INFO, "Game", "OpenGL renderer: %s", glGetString(GL_RENDERER));
	__android_log_print(ANDROID_LOG_INFO, "Game", "OpenGL version: %s", glGetString(GL_VERSION));
	__android_log_print(ANDROID_LOG_INFO, "Game", "OpenGL extensions: %s", glGetString(GL_EXTENSIONS));

	// Todo(Leo): maybe use this, we don't really need it, since we only ever draw 2d quads, but maybe 
	// its more expensive not to cull. Research (study) first.
	// glEnable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	log_info("Done initializing opengl");
	return context;
}

internal void terminate_opengl(GLContext * context)
{
	eglMakeCurrent(context->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(context->display);
}

enum
{
	VIEW_DRAW,
	VIEW_MENU,
	VIEW_TRANSITION_TO_DRAW,
	VIEW_TRANSITION_TO_MENU,
};

// Note(Leo): these map directly to values in brush shader, so explicitly define their values
enum : int32
{
	BRUSH_DRAW 	= 0,
	BRUSH_ERASE = 1,
};

struct Game
{
	bool32 initialized = false;
	bool32 running;

	GLContext context;
	bool canvasStoredToFile;

	// ----------------------------------------------
	GLuint brushShaderId;
	GLuint brushMaskTextureId;
	GLuint brushGradientTexture0;
	GLuint brushGradientTexture1;
	GLuint brushGradientTexture;
	int brushGradientTextureIndex;

	GLuint canvasShaderId;
	GLuint canvasTextureId;
	GLuint canvasFramebuffer;

	GLuint quadShader;
	GLuint buttonTextTexture;

	GLuint creditsTexture;

	// Note(Leo): From top left
	// Todo(Leo): Compute according to actual screen
	v2 clearCanvasPosition 		= {240, 1280 * (1.0f / 11.0f)};
	v2 clearCanvasSize			= {240, 1280 * (4.0f / 11.0f)};

	v2 creditsPosition 			= {240, 1280 * (6.0f / 11.0f)};

	// ----------------------------------------------
	
	static constexpr float minBrushSize 		= 25;
	static constexpr float maxBrushSize 		= 75;
	static constexpr float maxBrushSizeTimeMS 	= 500;

	static constexpr float drawViewPosition 		= 0.0f;
	static constexpr float menuViewPosition 		= 1.0f;
	static constexpr float viewTransitionDuration 	= 0.4f;

	int state 			= VIEW_DRAW; 
	float viewPosition 	= drawViewPosition;

	int brushMode 		= BRUSH_DRAW;

	static constexpr float doubleTapTimeThreshold = 0.5f;

	v2 			lastTouchPosition;
	v2 			lastStrokeDirection;
	v2 			currentTouchPosition;
	bool32 currentStrokeUpdated;

	timespec 	touchDownTime;
	bool 		strokeMoved;
	float 		strokeWidth;

	/* Todo(Leo): Add stroke points to this, and always update a few frames late,
	Main function should dequeue one frame alway after input if queue is full, and 
	it has been refreshed*/
	static constexpr int drawPositionQueueCapacity = 10;
	v2 drawPositionQueue [drawPositionQueueCapacity];
	int drawPositionQueueCount;
	int drawPositionQueueFlushCount;
	bool32 drawPositionQueueRefreshed;

	v2 lastDequedDrawPosition;

	float currentStrokeLength;
	float lastStrokeSectionLength;
	float currentStrokeColourSelection;

	// ----------------------------------------------

	// Main loop will be run in separate thread, apparently standard android thing
	pthread_t 		thread;
	pthread_mutex_t mutex;
	pthread_cond_t 	cond;

	int msgread;
	int msgwrite;

	void* 	savedState;
	size_t 	savedStateSize;
	int 	canvasFile;

	int stateSaved;
	int destroyed;

	ANativeActivity* 	activity;
	AConfiguration* 	config;
	ALooper* 			looper;

	ANativeWindow* 		window;
	AInputQueue* 		inputQueue;

	// Todo(Leo): we probably want to take this into account too
	// ARect 				contentRect;

	// Todo(Leo): Check if we really need pending stuffs
	ANativeWindow* 		pendingWindow;
	AInputQueue* 		pendingInputQueue;
	// ARect 				pendingContentRect;
};

internal void clear_canvas(Game * game)
{
	glBindFramebuffer(GL_FRAMEBUFFER, game->canvasFramebuffer);
	glViewport(0, 0, game->context.width, game->context.height);
	glClearColor(1, 1, 1, 1);
	glClear(GL_COLOR_BUFFER_BIT);
}

internal void generate_gradient_texture_strip(int colourCount, v4 * colours, int pixelCount, uint8 * pixelMemory)
{
	int colourIndex 		= 0;

	for (int pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
	{
		float interpolatonTime = (float)pixelIndex / (pixelCount - 1);

		// Note(Leo): RGBA components
		int componentIndex = pixelIndex * 4;

		constexpr int R = 0, G = 1, B = 2, A = 0;
		uint8 r, g, b;

		// Note(Leo): Currently just skip values that fall in between two steps. That should not
		// be a problem though, we have like 3 - 5 colours at max and texture resolution is much higher.
		while (colourIndex < colourCount && interpolatonTime > colours[colourIndex].t)
		{
			colourIndex += 1;
		}

		if (colourIndex == 0)
		{
			r = uint8(colours[0].r * 255);
			g = uint8(colours[0].g * 255);
			b = uint8(colours[0].b * 255);
		}
		else if (colourIndex == colourCount)
		{
			r = uint8(colours[colourCount - 1].r * 255);
			g = uint8(colours[colourCount - 1].g * 255);
			b = uint8(colours[colourCount - 1].b * 255);
		}
		else
		{
			float previousTime 				= colours[colourIndex - 1].t;
			float nextTime 					= colours[colourIndex].t;
			float localInterpolationTime 	= (interpolatonTime - previousTime) / (nextTime - previousTime);

			v3_hsv previousColourHSV 	= hsv_from_rgb(rgb(colours[colourIndex - 1]));
			v3_hsv nextColourHSV 		= hsv_from_rgb(rgb(colours[colourIndex]));

			v3_hsv interpolatedColorHSV = v3_hsv_lerp(previousColourHSV, nextColourHSV, localInterpolationTime);

			v3 interpolatedColor 			= v3_lerp(rgb(colours[colourIndex - 1]), rgb(colours[colourIndex]), localInterpolationTime);

			interpolatedColor = rgb_from_hsv(hsv_from_rgb(interpolatedColor));

			r = uint8(interpolatedColor.r * 255);
			g = uint8(interpolatedColor.g * 255);
			b = uint8(interpolatedColor.b * 255);
		}

		pixelMemory[componentIndex + R] = r;
		pixelMemory[componentIndex + G] = g;
		pixelMemory[componentIndex + B] = b;
		// pixelMemory[componentIndex + A] = 255;

		// __android_log_print(ANDROID_LOG_INFO, "Game", "generate gradient, %i, r = %i, g = %i, b = %i", componentIndex, (int)r, (int)g, (int)b);
	}

	{
		v3 test = {0, 1, 0};
		v3_hsv testHSV= hsv_from_rgb(test);

		__android_log_print(ANDROID_LOG_INFO, "Game", "hsv test = %f, %f, %f", testHSV.h, testHSV.s, testHSV.v);	

		test = rgb_from_hsv(testHSV);

		__android_log_print(ANDROID_LOG_INFO, "Game", "rgb test = %f, %f, %f", test.r, test.g, test.b);	

	}
}

internal void initialize_shaders(Game * game)
{
	auto load_shader = [](const char * source, GLenum type) ->GLuint
	{
		GLuint shader = glCreateShader(type);

		if (shader == 0)
		{
			log_error("Shader creation failed");
			log_error(gl_error_string(glGetError()));
			return 0;
		}

		glShaderSource(shader, 1, &source, nullptr);
		glCompileShader(shader);


		GLint compiled;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

		if (compiled == false)
		{
			char logBuffer [512];
			glGetShaderInfoLog(shader, 512, nullptr, logBuffer);

			log_error("Shader compilation failed");
			log_error(logBuffer);
			glDeleteShader(shader);
			return 0;
		}
		else
		{
			log_info("Shader compilation SUCCESS");
		}

		return shader;
	};

	auto log_gl_shader_program = [](GLuint program)
	{
		GLsizei length;
		char buffer [1024];
		glGetProgramInfoLog(program, 1024, &length, buffer);

		__android_log_print(ANDROID_LOG_INFO, "Game", "Shader program log (%d): %s", program, buffer);
	};


	{
		const char constexpr * brushVertexShaderSource =
		R"(	#version 300 es
			in vec4 vertex;

			uniform mat4 projection;
			uniform mat4 view;
			uniform mat4 model;

			out vec2 uv;

			void main()
			{
				gl_Position = projection * view * model * vec4(vertex.xy, 0.0, 1.0);
				uv = vertex.zw;
			}
		)";

		const char constexpr * brushFragmentShaderSource =
		R"(	#version 300 es
			precision mediump float;

			in vec2 uv;

			uniform sampler2D 	brushTexture;
			uniform sampler2D 	gradientColor;
			uniform float 		gradientPosition;

			#define BRUSH_DRAW 0
			#define BRUSH_ERASE 1

			uniform int brushMode;
			
			uniform vec3 color;

			out vec4 fragColor;
			void main()
			{
				float alpha = texture(brushTexture, uv).r;
				
				if (brushMode == BRUSH_DRAW)
				{
					vec4 color_ = texture(gradientColor, vec2(gradientPosition, 0));
					fragColor = vec4(color_.rgb, alpha);
				}
				else if (brushMode == BRUSH_ERASE)
				{
					fragColor = vec4(1,1,1, alpha);
				}
			}
		)";

		GLuint brushVertexShader = load_shader(brushVertexShaderSource, GL_VERTEX_SHADER);
		GLuint brushFragmentShader = load_shader(brushFragmentShaderSource, GL_FRAGMENT_SHADER);

		// Todo(Leo): This can fail
		game->brushShaderId = glCreateProgram();

		glAttachShader(game->brushShaderId, brushVertexShader);
		glAttachShader(game->brushShaderId, brushFragmentShader);
		glLinkProgram(game->brushShaderId);


		{
			char const * brushNames [] =
			{
				"brush_0.png",
				"brush_1.png",
				"brush_2.png",
			};
			char const * brushName = brushNames[0];

			glGenTextures(1, &game->brushMaskTextureId);

			AAssetManager * assetManager 	= game->activity->assetManager;
			AAsset * textureAsset 			= AAssetManager_open(assetManager, brushName, AASSET_MODE_BUFFER);
			int length 						= AAsset_getLength(textureAsset);
			uint8 * buffer 					= (uint8*)AAsset_getBuffer(textureAsset);

			// Note(Leo): assume 4 channel textures
			int width, height, channels;
			uint8 * textureMemory 			= stbi_load_from_memory(buffer, length, &width, &height, &channels, 4);

			glBindTexture(GL_TEXTURE_2D, game->brushMaskTextureId);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureMemory);

			glGenerateMipmap(GL_TEXTURE_2D);

			stbi_image_free(textureMemory);
		}

		int gradientPixelCount = 128;
		uint8 * gradientTextureMemory = new uint8[gradientPixelCount * 4];

		v4 gradientValues_0 [] = 
		{
			204.0f / 255, 38.0f / 255, 0, 		0.3f,
			1, 230.0f / 255, 200.0f / 255, 		0.45f,
			0, 230.0f /255, 1, 					0.6f,
		};

		generate_gradient_texture_strip(3, gradientValues_0, gradientPixelCount, gradientTextureMemory);

		GLuint brushGradientTexture0;
		glGenTextures(1, &brushGradientTexture0);
		glBindTexture(GL_TEXTURE_2D, brushGradientTexture0);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gradientPixelCount, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gradientTextureMemory);

		game->brushGradientTexture0 	= brushGradientTexture0;
		game->brushGradientTexture 		= game->brushGradientTexture0;
		game->brushGradientTextureIndex = 0;


		v4 gradientValues_1 [] = 
		{
			0.352, 0.858, 0.556, 	0.15,
			1, 0.494, 0.176, 		0.4,
			1, 0.956, 0.301, 		0.59,
		};

		generate_gradient_texture_strip(3, gradientValues_1, gradientPixelCount, gradientTextureMemory);


		GLuint brushGradientTexture1;
		glGenTextures(1, &brushGradientTexture1);
		glBindTexture(GL_TEXTURE_2D, brushGradientTexture1);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gradientPixelCount, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gradientTextureMemory);

		game->brushGradientTexture1 = brushGradientTexture1;

		delete [] gradientTextureMemory;
	}

	/// CANVAS
	{
		const char constexpr * canvasVertexShaderSource =
		R"(	#version 300 es
			in vec4 position;

			out vec2 uv;
			void main()
			{
				gl_Position = vec4(position.xy, 0, 1);
				uv 			= position.zw;
			}
		)";

		const char constexpr * canvasFragmentShaderSource =
		R"(	#version 300 es
			precision mediump float;

			in vec2 uv;

			uniform sampler2D canvasTexture;

			out vec4 fragColor;
			void main()
			{
				fragColor = texture(canvasTexture, uv);
			}
		)";


		GLuint canvasVertexShader = load_shader(canvasVertexShaderSource, GL_VERTEX_SHADER);
		GLuint canvasFragmentShader = load_shader(canvasFragmentShaderSource, GL_FRAGMENT_SHADER);

		game->canvasShaderId = glCreateProgram();
		glAttachShader(game->canvasShaderId, canvasVertexShader);
		glAttachShader(game->canvasShaderId, canvasFragmentShader);
		glLinkProgram(game->canvasShaderId);

		GLuint canvasTexture;
		glGenTextures(1, &canvasTexture);
		glBindTexture(GL_TEXTURE_2D, canvasTexture);

		int screenWidth = game->context.width;
		int screenHeight = game->context.height;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, screenWidth, screenHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

		game->canvasTextureId = canvasTexture;

		glGenFramebuffers(1, &game->canvasFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, game->canvasFramebuffer);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, game->canvasTextureId, 0);

		clear_canvas(game);

		log_gl_shader_program(game->canvasShaderId);

		__android_log_print(ANDROID_LOG_INFO, "Game", "Framebuffer Status = %s", gl_framebuffer_status_string(glCheckFramebufferStatus(GL_FRAMEBUFFER)));
	// }

		// Todo...
		// Note(Leo): Im not sure about these, this is unbinding
	//    glActiveTexture(0);
	//    glBindTexture(GL_TEXTURE_2D, 0);

	}

	/// QUADS
	{
		const char constexpr * quadVertexShaderSource =
		R"(	#version 300 es
			in vec4 vertex;

			out vec2 texcoord;

			void main()
			{
				gl_Position = vec4(vertex.xy, 0, 1);
				texcoord 	= vertex.zw;
			}
		)";

		const char constexpr * quadFragmentShaderSource =
		R"(	#version 300 es
			precision mediump float;

			in vec2 texcoord;

			uniform sampler2D _texture;

			#define TEXT_MODE 0
			#define IMAGE_MODE 1

			uniform int mode;

			out vec4 outColor;

			void main()
			{
				if (mode == TEXT_MODE)
				{
					// Todo(Leo): change to alpha or red, and also to alpha or red only textures
					outColor.rgb = vec3(0.1, 0.05, 0.05);
					outColor.a = texture(_texture, texcoord).b;
				}
				else if (mode == IMAGE_MODE)
				{
					outColor = texture(_texture, texcoord);
				}
				else
				{
					outColor = vec4(1,0,1,1);
				}

				// float a = texture(_texture, texcoord).g;
				// outColor = vec4(a,a,a,1); 
				// outColor = vec4(0.8,0.2, 0.2,1); 
			}
		)";

		GLuint quadVertexShader 	= load_shader(quadVertexShaderSource, GL_VERTEX_SHADER);
		GLuint quadFragmentShader 	= load_shader(quadFragmentShaderSource, GL_FRAGMENT_SHADER);

		game->quadShader = glCreateProgram();
		glAttachShader(game->quadShader, quadVertexShader);
		glAttachShader(game->quadShader, quadFragmentShader);
		glLinkProgram(game->quadShader);


		{
			glGenTextures(1, &game->buttonTextTexture);

			AAssetManager * assetManager 	= game->activity->assetManager;
			AAsset * textureAsset 			= AAssetManager_open(assetManager, "clear_link.png", AASSET_MODE_BUFFER);
			int length 						= AAsset_getLength(textureAsset);
			uint8 * buffer 					= (uint8*)AAsset_getBuffer(textureAsset);

			// Note(Leo): assume 4 channel textures
			int width, height, channels;
			uint8 * textureMemory 			= stbi_load_from_memory(buffer, length, &width, &height, &channels, 4);

			glBindTexture(GL_TEXTURE_2D, game->buttonTextTexture);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureMemory);

			stbi_image_free(textureMemory);
		}

		/// CREDITS TEXTURE
		{
			glGenTextures(1, &game->creditsTexture);

			AAssetManager * assetManager 	= game->activity->assetManager;
			AAsset * textureAsset 			= AAssetManager_open(assetManager, "credits.png", AASSET_MODE_BUFFER);
			int length 						= AAsset_getLength(textureAsset);
			uint8 * buffer 					= (uint8*)AAsset_getBuffer(textureAsset);

			// Note(Leo): assume 4 channel textures
			int width, height, channels;
			uint8 * textureMemory 			= stbi_load_from_memory(buffer, length, &width, &height, &channels, 4);

			glBindTexture(GL_TEXTURE_2D, game->creditsTexture);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureMemory);

			stbi_image_free(textureMemory);
		}
	}
}

internal void draw_brush(Game * game, v2 screenPosition, float size, float gradientPosition)
{
	GLfloat vertices [] =
	{
		-0.5, -0.5, 0, 0,
		 0.5, -0.5, 1, 0,
		-0.5,  0.5, 0, 1,
		 0.5,  0.5, 1, 1,
	};

	GLfloat projection [] =
	{
		2 / (float)game->context.width, 0, 0, 0,
		0, 2 / (float)game->context.height, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	GLfloat view [] =
	{
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	float x = screenPosition.x - (game->context.width / 2);
	float y = (game->context.height / 2) - screenPosition.y;

	GLfloat model [] =
	{
		size, 0, 0, 0,
		0, size, 0, 0,
		0, 0, 1, 0,
		x, y, 0, 1,
	};

	glUseProgram(game->brushShaderId);

	// Todo(Leo): get these only when init opengl
	// Todo(Leo): really actually maybe just define these in shader with layout like with vulkan
	GLint projectionLocation 		= glGetUniformLocation(game->brushShaderId, "projection");
	GLint viewLocation				= glGetUniformLocation(game->brushShaderId, "view");
	GLint modelMatrixLocation 		= glGetUniformLocation(game->brushShaderId, "model");
	GLint brushTextureLocation 		= glGetUniformLocation(game->brushShaderId, "brushTexture");
	GLint colorLocation				= glGetUniformLocation(game->brushShaderId, "color");
	GLint gradientTextureLocation 	= glGetUniformLocation(game->brushShaderId, "gradientColor");
	GLint gradientPositionLocation 	= glGetUniformLocation(game->brushShaderId, "gradientPosition");
	GLint brushModeLocation 		= glGetUniformLocation(game->brushShaderId, "brushMode");

	// Bind canvas framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, game->canvasFramebuffer);
	glViewport(0, 0, game->context.width, game->context.height);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, vertices);
	glEnableVertexAttribArray(0);

	glUniformMatrix4fv(projectionLocation, 1, false, projection);
	glUniformMatrix4fv(viewLocation, 1, false, view);
	glUniformMatrix4fv(modelMatrixLocation, 1, false, model);

	glUniform1i(brushTextureLocation, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, game->brushMaskTextureId);

	glUniform1i(gradientTextureLocation, 1);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, game->brushGradientTexture);

	glUniform1f(gradientPositionLocation, gradientPosition);
	glUniform1i(brushModeLocation, game->brushMode);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable( GL_BLEND );

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(0);
}

internal void draw_canvas(Game * game)
{
	float tweenedPosition;
	{
		float i = std::floor(game->viewPosition);
		float f = game->viewPosition - i;
		f = f * (f * (f * -2 + 3));
		tweenedPosition = i + f;
		}

	// Note(Leo): Draw the mighty "full screen triangle"
	GLfloat canvasVertices [] =
	{
		-1 + 2 * (tweenedPosition - game->drawViewPosition), -1, 0, 0,
		 3 + 2 * (tweenedPosition - game->drawViewPosition), -1, 2, 0,
		-1 + 2 * (tweenedPosition - game->drawViewPosition),  3, 0, 2,
	};

	// Bind screen framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, game->context.width, game->context.height);

	glClearColor(1,1,1,1);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(game->canvasShaderId);
	glDisable( GL_BLEND );

	// Todo(Leo): read once in init place
	GLint textureLocation 			= glGetUniformLocation(game->canvasShaderId, "canvasTexture");

	glUniform1i(textureLocation, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, game->canvasTextureId);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, canvasVertices);
	glEnableVertexAttribArray(0);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

	// Unbind so we can draw to this on next frame
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisableVertexAttribArray(0);

	/// ------------------------------------------------------
	/// BUTTONS

	// Todo(Leo): better naming, what fukin context
	auto compute_quad_vertices = [&context = game->context](GLfloat (&vertexArray)[16], v2 position, v2 size, v2 uvStart, v2 uvEnd)
	{	
		size.x /= (context.width / 2);
		size.y /= (context.height / 2);

		position.x = position.x / (context.width / 2) - 1;
		position.y = 1 - position.y / (context.height / 2) - size.y; 


		struct Vertex
		{
			v2 position;
			v2 texcoord;
		};

		float x = position.x;
		float y = position.y;

		float w = size.x;
		float h = size.y;

		Vertex * vertices = (Vertex*)vertexArray;
		vertices[0] = { x, 		y, 		uvStart.x, 	uvStart.y };
		vertices[1] = { x + w, 	y, 		uvEnd.x, 	uvStart.y };
		vertices[2] = { x,  	y + h, 	uvStart.x, 	uvEnd.y };
		vertices[3] = { x + w,  y + h, 	uvEnd.x, 	uvEnd.y };
	};

	GLfloat quadVertices [16];

	glUseProgram(game->quadShader);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, quadVertices);
	glEnableVertexAttribArray(0);

	GLint buttonTextureLocation = glGetUniformLocation(game->quadShader, "_texture");
	GLint quadDrawModeLocation = glGetUniformLocation(game->quadShader, "mode");

	enum 
	{
		QUAD_MODE_TEXT 	= 0,
		QUAD_MODE_IMAGE = 1,
	};

	glActiveTexture(GL_TEXTURE0);

	v2 menuViewOffset = {(tweenedPosition - game->menuViewPosition) * game->context.width, 0};

	compute_quad_vertices(quadVertices, game->clearCanvasPosition + menuViewOffset, game->clearCanvasSize, {0,0}, {1,1});

	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, game->canvasTextureId);
	glUniform1i(quadDrawModeLocation, QUAD_MODE_IMAGE);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	compute_quad_vertices(quadVertices, game->creditsPosition + menuViewOffset, game->clearCanvasSize, {0,0}, {1,1});

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindTexture(GL_TEXTURE_2D, game->creditsTexture);
	glUniform1i(quadDrawModeLocation, QUAD_MODE_TEXT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(0);
}

internal void update_stroke(Game * game, v2 oneBeforeStrokeStart, v2 strokeStart, v2 strokeEnd, v2 oneAfterStrokeEnd)
{
		// Todo(Leo): Thoroughly evaluate these two
	constexpr float maxStrokeLength 			= 50;
	constexpr float strokeStartMoveThreshold 	= 10;

	if (game->strokeMoved == false)
	{
		float strokeLength = v2_magnitude(strokeEnd - strokeStart);

		if (strokeLength >= strokeStartMoveThreshold)
		{
			float timeSinceTouchDownMS 			= time_elapsed_milliseconds(game->touchDownTime);
			float interpolatonTime 				= float_clamp(timeSinceTouchDownMS / game->maxBrushSizeTimeMS, 0, 1);
			game->strokeWidth 					= float_lerp(game->minBrushSize, game->maxBrushSize, interpolatonTime);
			game->strokeMoved 					= true;
			game->lastStrokeSectionLength 		= strokeLength;
			game->currentStrokeColourSelection 	= float_clamp(strokeLength / maxStrokeLength, 0, 1);
		}
		else
		{
			return;
		}
	}

	/// --------------------------------------------------------

	// Note(Leo): roughly a third, and half to account for averages of in and out tangents
	float tangentScale = 0.16;

	v2 startInTangent = (strokeStart - oneBeforeStrokeStart);
	v2 startOutTangent = (strokeEnd - strokeStart);
	v2 startTangent = (startInTangent + startOutTangent) * tangentScale;

	v2 endInTangent = startOutTangent;
	v2 endOutTangent = (oneAfterStrokeEnd - strokeEnd);
	v2 endTangent = (endInTangent + endOutTangent) * tangentScale;

	v2 a = strokeStart;
	v2 b = strokeStart + startTangent;
	v2 c = strokeEnd - endTangent;
	v2 d = strokeEnd;

	struct ArcLengthMapEntry
	{
		float length;
		float t;
	};
	constexpr int precision = 10;
	ArcLengthMapEntry arcLengthMap[precision] = {{0, 0}};

	v2 previousArcPosition = strokeStart;
	for (int i = 1; i < precision; ++i)
	{
		float t 			= (float)i / (precision - 1);
		v2 nextArcPosition 	= v2_cubic_bezier_lerp(a,b,c,d, t);
		float arcLength 	= v2_magnitude(nextArcPosition - previousArcPosition);

		arcLengthMap[i].length 	= arcLengthMap[i - 1].length + arcLength;
		arcLengthMap[i].t 		= t;
		
		previousArcPosition 	= nextArcPosition;
	}

	float totalArcLength = arcLengthMap[precision - 1].length;

	float colourSelection = float_clamp(totalArcLength / maxStrokeLength, 0, 1);

	float drawDotArcLengthThreshold = game->strokeWidth / 10;
	int dotCount = static_cast<int>(totalArcLength / drawDotArcLengthThreshold);

	for (int i = 0; i < dotCount; ++i)
	{
		float t = static_cast<float>(i) / (dotCount - 1);
		float targetArcLength = t * totalArcLength;

		int index = 0;
		while(arcLengthMap[index].length > targetArcLength)
		{
			index += 1;
		}

		auto previousArcPoint 	= arcLengthMap[index];
		auto nextArcPoint 		= arcLengthMap[index + 1];

		float tt = (targetArcLength - previousArcPoint.length) / (nextArcPoint.length - previousArcPoint.length);
		t = float_lerp(previousArcPoint.t, nextArcPoint.t, tt);

		v2 dotPosition = v2_cubic_bezier_lerp(a,b,c,d, t);

		float colorInterpolationTime = float_lerp(game->currentStrokeColourSelection, colourSelection, t);
		draw_brush(game, dotPosition, game->strokeWidth, colorInterpolationTime);
	}

	game->lastStrokeSectionLength 		= totalArcLength;
	game->currentStrokeLength 			+= totalArcLength;
	game->currentStrokeColourSelection 	= float_lerp(game->currentStrokeColourSelection, colourSelection, 0.2);

#if 0
	v2 stroke 				= endPosition - startPosition;
	float strokeLength 		= v2_magnitude(stroke);


	/*
	Todo(Leo): Currently we track lenght of stroke for color, but we would like to track
	speed, so use time stuff for that.
	*/

	float drawStrokeLengthThreshold = game->strokeWidth / 10;
	int dotCount 					= std::floor(strokeLength / drawStrokeLengthThreshold);

	v2 dotPosition 			= startPosition;
	v2 dotPositionStep 		= {stroke.x / dotCount, stroke.y / dotCount};

	strokeLength 			= float_lerp(game->lastStrokeSectionLength, strokeLength, 0.1);

	float strokeLengthDelta = strokeLength - game->lastStrokeSectionLength;
	float strokeLengthStep 	= strokeLengthDelta / dotCount;
	float strokeLengthState = game->lastStrokeSectionLength;

	v2 previousTangent 	= game->lastStrokeDirection;
	v2 nextTangent 		= v2_normalize(stroke);

	v2 midPosition;

	if (false)
	{
		float tangentScale 		= 1000;
		v2 previousControlPoint = startPosition + previousTangent * tangentScale;
		v2 nextControlPoint 	= endPosition - nextTangent * tangentScale;
		midPosition 			= (previousControlPoint + nextControlPoint) * 0.5;
	}


	{
		// float angle 	= v2_angle(previousTangent, (endPosition - startPosition)); 
		v2 strokeVector = endPosition - startPosition;
		float dot 		= v2_dot(v2_normalize(strokeVector), previousTangent);
		if (dot > 0.9999f)
		{
			midPosition = startPosition + (endPosition - startPosition) / 2;
		}
		else
		{
			// float cosAngle = std::cos(angle);
			float adjacent 	= v2_magnitude(endPosition - startPosition) / 2;
			float hypotenuse = adjacent / dot;

			midPosition = startPosition + previousTangent * hypotenuse;
			nextTangent = endPosition - midPosition;
		}
	}

	// constexpr int precision 		= 20;
	// constexpr float precisionStep 	= 1.0f / (precision - 1);

	// float arcLengths [precision] 	= {0};
	// v2 previous 					= startPosition;
	// for (int i = 1; i < precision; ++i)
	// {
	// 	float t 		= i * precisionStep;
	// 	v2 current 		= v2_bezier_lerp(startPosition, midPosition, endPosition, t);
	// 	arcLengths[i] 	= arcLengths[i - 1] + v2_magnitude(current - previous);
	// 	previous 		= current; 
	// }

	// float totalArcLength = arcLengths[precision - 1];

	for (int i = 0; i < dotCount; ++i)
	{
		float t = (float)i / (dotCount - 1);
		// float currentArcLength = t * totalArcLength;

		// float previousArcLength;
		// float nextArcLength;

		// int arcIndex = 0;
		// while(arcLengths[arcIndex] < currentArcLength)
		// {
		// 	previousArcLength = arcLengths[arcIndex];
		// 	nextArcLength = arcLengths[arcIndex + 1];

		// 	arcIndex += 1;
		// }

		// float tt = (currentArcLength - previousArcLength) / (nextArcLength - previousArcLength);

		// float tt = (T - arcLengths[arcIndex]) / (arcLengths[arcIndex + 1] - arcLengths[arcIndex]); 

		// t = (arcIndex + tt) * precisionStep;

		// __android_log_print(ANDROID_LOG_INFO, "Game", "tt = %f", tt);

		// v2 dotPosition2 = v2_lerp(startPosition, endPosition, t);

		v2 dotPosition2 = v2_bezier_lerp(startPosition, midPosition, endPosition, t);
		dotPosition2 = v2_lerp(startPosition, endPosition, t);



		float colorInterpolationTime 	= float_clamp(strokeLengthState / maxStrokeLength, 0, 1);
		draw_brush(game, dotPosition2, game->strokeWidth, colorInterpolationTime);

		dotPosition 		= dotPosition + dotPositionStep;
		strokeLengthState 	+= strokeLengthStep;
	}

	float colorInterpolationTime 	= float_clamp(strokeLength / maxStrokeLength, 0, 1);
	draw_brush(game, endPosition, game->strokeWidth, colorInterpolationTime);

	// game->lastTouchPosition = endPosition;
	game->lastStrokeSectionLength = strokeLength;
	game->lastStrokeDirection = nextTangent;

#endif
}

enum
{
	LOOPER_ID_MAIN 	= 1,
	LOOPER_ID_INPUT = 2,
	LOOPER_ID_USER 	= 3,
};

enum
{
	APP_CMD_INPUT_CHANGED,
	APP_CMD_INIT_WINDOW,
	APP_CMD_TERM_WINDOW,
	APP_CMD_WINDOW_RESIZED,
	APP_CMD_WINDOW_REDRAW_NEEDED,
	APP_CMD_CONTENT_RECT_CHANGED,
	APP_CMD_GAINED_FOCUS,
	APP_CMD_LOST_FOCUS,
	APP_CMD_CONFIG_CHANGED,
	APP_CMD_LOW_MEMORY,
	APP_CMD_START,
	APP_CMD_RESUME,
	APP_CMD_SAVE_STATE,
	APP_CMD_PAUSE,
	APP_CMD_STOP,
	APP_CMD_DESTROY,
};

internal void free_saved_state(Game * game)
{
	pthread_mutex_lock(&game->mutex);
	if (game->savedState != NULL) {
		free(game->savedState);
		game->savedState = NULL;
		game->savedStateSize = 0;
	}
	pthread_mutex_unlock(&game->mutex);
}

 
/*
Todo(Leo): Study and understand android_app_read_cmd and android_app_write_cmd functions
They read and write to same file descriptors (apparently, maybe this is wrong) and that
way talk to different threads.

Basically android callbacks use write end of pipe, and our main thread uses read end of pipe
*/

internal int8_t android_app_read_cmd(Game * game)
{
	int8_t cmd;
	if (read(game->msgread, &cmd, sizeof(cmd)) == sizeof(cmd)) {
		switch (cmd) {
			case APP_CMD_SAVE_STATE:
				free_saved_state(game);
				break;
		}
		return cmd;
	} else {
		__android_log_print(ANDROID_LOG_ERROR, "Game", "No data on command pipe!");
	}
	return -1;
}

internal void android_app_write_cmd(Game * game, int8_t cmd)
{
	if (write(game->msgwrite, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		__android_log_print(ANDROID_LOG_ERROR, "Game", "Failure writing android_app cmd: %s\n", strerror(errno));
	}
}

internal void print_cur_config(Game * game)
{
	char lang[2], country[2];
	AConfiguration_getLanguage(game->config, lang);
	AConfiguration_getCountry(game->config, country);

	GLUE_LOGV("Config: mcc=%d mnc=%d lang=%c%c cnt=%c%c orien=%d touch=%d dens=%d "
			"keys=%d nav=%d keysHid=%d navHid=%d sdk=%d size=%d long=%d "
			"modetype=%d modenight=%d",
			AConfiguration_getMcc(game->config),
			AConfiguration_getMnc(game->config),
			lang[0], lang[1], country[0], country[1],
			AConfiguration_getOrientation(game->config),
			AConfiguration_getTouchscreen(game->config),
			AConfiguration_getDensity(game->config),
			AConfiguration_getKeyboard(game->config),
			AConfiguration_getNavigation(game->config),
			AConfiguration_getKeysHidden(game->config),
			AConfiguration_getNavHidden(game->config),
			AConfiguration_getSdkVersion(game->config),
			AConfiguration_getScreenSize(game->config),
			AConfiguration_getScreenLong(game->config),
			AConfiguration_getUiModeType(game->config),
			AConfiguration_getUiModeNight(game->config));
}

// Note(Leo): this is called in game main loop thread, and not in android callback thread
internal void process_input(Game * game)
{
	AInputEvent* event = NULL;
	while (AInputQueue_getEvent(game->inputQueue, &event) >= 0)
	{
		GLUE_LOGV("New input event: type=%d\n", AInputEvent_getType(event));
		if (AInputQueue_preDispatchEvent(game->inputQueue, event) != 0)
		{
			continue;
		}

		bool32 handled = false;
		switch (AInputEvent_getType(event))
		{
			case AINPUT_EVENT_TYPE_MOTION:
			{
				// constexpr float minBrushSize 		= 25;
				// constexpr float maxBrushSize 		= 75;
				// constexpr float maxBrushSizeTimeMS 	= 500;

				// Todo(Leo): Check all of these pointer indices
				switch (AMotionEvent_getAction(event))
				{
					case AMOTION_EVENT_ACTION_DOWN:
					{
						if (game->state == VIEW_DRAW)
						{
							float timeSinceLastTouchDown = time_elapsed_seconds(game->touchDownTime);
							if (timeSinceLastTouchDown < game->doubleTapTimeThreshold)
							{
								game->brushMode = BRUSH_ERASE;
							}

							game->lastTouchPosition = {AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0)};

							game->strokeMoved 		= false;
							game->lastStrokeSectionLength 	= 0;
						}

						game->touchDownTime 	= time_now();
					} break;

					case AMOTION_EVENT_ACTION_UP:
					{
						// Note(Leo): set this regardless of view mode, we might have changed mode here
						game->brushMode = BRUSH_DRAW;

						if (game->state == VIEW_MENU)
						{
							v2 touchPosition = {AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0)};

							auto test_button_rect = [touchPosition](v2 position, v2 size) -> bool32
							{
								v2 min = position;
								v2 max = position + size;

								bool32 inside = touchPosition.x > min.x
												&& touchPosition.x < max.x
												&& touchPosition.y > min.y
												&& touchPosition.y < max.y;

								return inside;
							};

							if (test_button_rect(game->clearCanvasPosition, game->clearCanvasSize))
							{
								log_info("Clear canvas");	

								game->brushGradientTextureIndex += 1;
								game->brushGradientTextureIndex %= 2;

								if (game->brushGradientTextureIndex == 0)
								{
									game->brushGradientTexture = game->brushGradientTexture0;
								}
								else if (game->brushGradientTextureIndex == 1)
								{
									game->brushGradientTexture = game->brushGradientTexture1;
								}
								
								clear_canvas(game);
							}
						}
						else if(game->state == VIEW_DRAW)
						{
							if (game->strokeMoved == false)
							{
								float timeSinceTouchDownMS 	= time_elapsed_milliseconds(game->touchDownTime);
								float interpolatonTime 		= float_clamp(timeSinceTouchDownMS / game->maxBrushSizeTimeMS, 0, 1);
								float strokeWidth 			= float_lerp(game->minBrushSize, game->maxBrushSize, interpolatonTime);

								draw_brush(game, game->lastTouchPosition, strokeWidth, 0);
							}
						}
					} break;

					case AMOTION_EVENT_ACTION_MOVE:
					{
						if (game->state != VIEW_DRAW)
							break;

						v2 currentTouchPosition = {AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0)};

						game->drawPositionQueue[game->drawPositionQueueCount] = currentTouchPosition;
						game->drawPositionQueueCount += 1;
						game->drawPositionQueueRefreshed = true;

						__android_log_print(ANDROID_LOG_INFO, "Game", "push to draw queue %d", game->drawPositionQueueCount);


						// game->currentTouchPosition = currentTouchPosition;
						// game->currentStrokeUpdated = true;
						handled = true;

						#if 0
						// Todo(Leo): Thoroughly evaluate these two
						constexpr float maxStrokeLength 			= 80;
						constexpr float strokeStartMoveThreshold 	= 10;

						v2 currentTouchPosition = {AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0)};
						v2 stroke 				= currentTouchPosition - game->lastTouchPosition;
						float strokeLength 		= v2_magnitude(stroke);

						if (game->strokeMoved == false)
						{
							if (strokeLength >= strokeStartMoveThreshold)
							{
								float timeSinceTouchDownMS 	= time_elapsed_milliseconds(game->touchDownTime);
								float interpolatonTime 		= float_clamp(timeSinceTouchDownMS / maxBrushSizeTimeMS, 0, 1);
								game->strokeWidth 			= float_lerp(minBrushSize, maxBrushSize, interpolatonTime);
								game->strokeMoved 			= true;
								game->lastStrokeSectionLength 		= strokeLength;

								game->lastStrokeDirection 	= v2_normalize(stroke);
							}
							else
							{
								break;
							}
						}

						/*
						Todo(Leo): Currently we track lenght of stroke for color, but we would like to track
						speed, so use time stuff for that.
						*/

						float drawStrokeLengthThreshold = game->strokeWidth / 10;
						int dotCount 					= std::floor(strokeLength / drawStrokeLengthThreshold);

						v2 dotPosition 			= game->lastTouchPosition;
						v2 dotPositionStep 		= {stroke.x / dotCount, stroke.y / dotCount};

						strokeLength 			= float_lerp(game->lastStrokeSectionLength, strokeLength, 0.1);

						v2 startPosition = game->lastTouchPosition;
						v2 endPosition = currentTouchPosition;


						float strokeLengthDelta = strokeLength - game->lastStrokeSectionLength;
						float strokeLengthStep 	= strokeLengthDelta / dotCount;
						float strokeLengthState = game->lastStrokeSectionLength;

						v2 previousTangent 	= game->lastStrokeDirection;
						v2 nextTangent 		= v2_normalize(stroke);

						v2 midPosition;

						if (false)
						{
							float tangentScale 		= 1000;
							v2 previousControlPoint = startPosition + previousTangent * tangentScale;
							v2 nextControlPoint 	= endPosition - nextTangent * tangentScale;
							midPosition 			= (previousControlPoint + nextControlPoint) * 0.5;
						}


						{
							// float angle 	= v2_angle(previousTangent, (endPosition - startPosition)); 
							v2 strokeVector = endPosition - startPosition;
							float dot 		= v2_dot(v2_normalize(strokeVector), previousTangent);
							if (dot > 0.9999f)
							{
								midPosition = startPosition + (endPosition - startPosition) / 2;
							}
							else
							{
								// float cosAngle = std::cos(angle);
								float adjacent 	= v2_magnitude(endPosition - startPosition) / 2;
								float hypotenuse = adjacent / dot;

								midPosition = startPosition + previousTangent * hypotenuse;
								nextTangent = endPosition - midPosition;
							}
						}

						// constexpr int precision 		= 20;
						// constexpr float precisionStep 	= 1.0f / (precision - 1);

						// float arcLengths [precision] 	= {0};
						// v2 previous 					= startPosition;
						// for (int i = 1; i < precision; ++i)
						// {
						// 	float t 		= i * precisionStep;
						// 	v2 current 		= v2_bezier_lerp(startPosition, midPosition, endPosition, t);
						// 	arcLengths[i] 	= arcLengths[i - 1] + v2_magnitude(current - previous);
						// 	previous 		= current; 
						// }

						// float totalArcLength = arcLengths[precision - 1];

						for (int i = 0; i < dotCount; ++i)
						{
							float t = (float)i / (dotCount - 1);
							// float currentArcLength = t * totalArcLength;

							// float previousArcLength;
							// float nextArcLength;

							// int arcIndex = 0;
							// while(arcLengths[arcIndex] < currentArcLength)
							// {
							// 	previousArcLength = arcLengths[arcIndex];
							// 	nextArcLength = arcLengths[arcIndex + 1];

							// 	arcIndex += 1;
							// }

							// float tt = (currentArcLength - previousArcLength) / (nextArcLength - previousArcLength);

							// float tt = (T - arcLengths[arcIndex]) / (arcLengths[arcIndex + 1] - arcLengths[arcIndex]); 

							// t = (arcIndex + tt) * precisionStep;

							// __android_log_print(ANDROID_LOG_INFO, "Game", "tt = %f", tt);

							// v2 dotPosition2 = v2_lerp(startPosition, endPosition, t);

							v2 dotPosition2 = v2_bezier_lerp(startPosition, midPosition, endPosition, t);
							dotPosition2 = v2_lerp(startPosition, endPosition, t);



							float colorInterpolationTime 	= float_clamp(strokeLengthState / maxStrokeLength, 0, 1);
							draw_brush(game, dotPosition2, game->strokeWidth, colorInterpolationTime);

							dotPosition 		= dotPosition + dotPositionStep;
							strokeLengthState 	+= strokeLengthStep;
						}


						draw_brush(game, midPosition, game->strokeWidth, 0);
						draw_brush(game, startPosition, game->strokeWidth / 2, 0);

						float colorInterpolationTime 	= float_clamp(strokeLength / maxStrokeLength, 0, 1);
						draw_brush(game, currentTouchPosition, game->strokeWidth, colorInterpolationTime);

						game->lastTouchPosition = currentTouchPosition;
						game->lastStrokeSectionLength = strokeLength;
						game->lastStrokeDirection = nextTangent;


						handled = true;
						#endif
					} break;
				}
			} break;

			case AINPUT_EVENT_TYPE_KEY:
			{	
				switch(AKeyEvent_getKeyCode(event))
				{
					case AKEYCODE_BACK:
					{
						// Todo(Leo): We get downs repeatedly when key is pressed, so we cannot use that as such. Instead manually
						// track downs and ups, and only proceed on first down after up
						if (AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_UP)
						{
							break;
						}

						if (game->state == VIEW_MENU)
						{
							game->state = VIEW_TRANSITION_TO_DRAW;
						}
						else if (game->state == VIEW_DRAW)
						{
							game->state = VIEW_TRANSITION_TO_MENU;
						}

						handled = true;
					} break;
				}
			} break;
		}

		AInputQueue_finishEvent(game->inputQueue, event, handled);
	}
}

internal void process_cmd(Game * game)
{
	int8_t cmd = android_app_read_cmd(game);

	//// PRE-PROCESS SOME COMMANDS
	switch (cmd)
	{
		case APP_CMD_INPUT_CHANGED:
			GLUE_LOGV("APP_CMD_INPUT_CHANGED\n");
			pthread_mutex_lock(&game->mutex);
			if (game->inputQueue != NULL) {
				AInputQueue_detachLooper(game->inputQueue);
			}
			game->inputQueue = game->pendingInputQueue;
			if (game->inputQueue != NULL)
			{
				GLUE_LOGV("Attaching input queue to looper");
				AInputQueue_attachLooper(game->inputQueue, game->looper, LOOPER_ID_INPUT, NULL, (void*)process_input);
			}
			pthread_cond_broadcast(&game->cond);
			pthread_mutex_unlock(&game->mutex);
			break;

		case APP_CMD_INIT_WINDOW:
			GLUE_LOGV("APP_CMD_INIT_WINDOW\n");
			pthread_mutex_lock(&game->mutex);
			game->window = game->pendingWindow;
			pthread_cond_broadcast(&game->cond);
			pthread_mutex_unlock(&game->mutex);
			break;

		case APP_CMD_TERM_WINDOW:
			GLUE_LOGV("APP_CMD_TERM_WINDOW\n");
			pthread_cond_broadcast(&game->cond);
			break;

		case APP_CMD_RESUME:
		case APP_CMD_START:
		case APP_CMD_PAUSE:
		case APP_CMD_STOP:
			break;

		case APP_CMD_CONFIG_CHANGED:
			GLUE_LOGV("APP_CMD_CONFIG_CHANGED\n");
			AConfiguration_fromAssetManager(game->config, game->activity->assetManager);
			print_cur_config(game);
			break;

		case APP_CMD_DESTROY:
			GLUE_LOGV("APP_CMD_DESTROY\n");
			((Game*)game)->running = false;
			break;
	}


	/// MID-PROCESS SOME COMMANDS 
	{
		auto android_app_cmd_string = [](int32_t cmd)
		{
			switch(cmd)
			{
				case APP_CMD_INPUT_CHANGED: 		return "APP_CMD_INPUT_CHANGED";
				case APP_CMD_INIT_WINDOW: 			return "APP_CMD_INIT_WINDOW";
				case APP_CMD_TERM_WINDOW: 			return "APP_CMD_TERM_WINDOW";
				case APP_CMD_WINDOW_RESIZED: 		return "APP_CMD_WINDOW_RESIZED";
				case APP_CMD_WINDOW_REDRAW_NEEDED: 	return "APP_CMD_WINDOW_REDRAW_NEEDED";
				case APP_CMD_CONTENT_RECT_CHANGED: 	return "APP_CMD_CONTENT_RECT_CHANGED";
				case APP_CMD_GAINED_FOCUS: 			return "APP_CMD_GAINED_FOCUS";
				case APP_CMD_LOST_FOCUS: 			return "APP_CMD_LOST_FOCUS";
				case APP_CMD_CONFIG_CHANGED: 		return "APP_CMD_CONFIG_CHANGED";
				case APP_CMD_LOW_MEMORY: 			return "APP_CMD_LOW_MEMORY";
				case APP_CMD_START: 				return "APP_CMD_START";
				case APP_CMD_RESUME: 				return "APP_CMD_RESUME";
				case APP_CMD_SAVE_STATE: 			return "APP_CMD_SAVE_STATE";
				case APP_CMD_PAUSE: 				return "APP_CMD_PAUSE";
				case APP_CMD_STOP: 					return "APP_CMD_STOP";
				case APP_CMD_DESTROY: 				return "APP_CMD_DESTROY";

				default:
					return "Unknown android_app command!";
			}
		};

		__android_log_print(ANDROID_LOG_INFO, "Game", "android_app cmd: %s", android_app_cmd_string(cmd));

		switch (cmd)
		{
			case APP_CMD_INIT_WINDOW:
			{
				if (game->initialized == false)
				{
					game->initialized = true;
					game->context = initialize_opengl(game->window);
					initialize_shaders (game);
				}

				if (game->canvasStoredToFile)
				{
					unsigned char pixel [4];

					int pixelDataSize 		= game->context.width * game->context.height * 4;
					int8_t * texturePixels 	= new int8_t[pixelDataSize];

					lseek(game->canvasFile, 0, SEEK_SET);
					// Todo(Leo): Check result and do something
					read(game->canvasFile, texturePixels, pixelDataSize);

					glBindTexture(GL_TEXTURE_2D, game->canvasTextureId);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, game->context.width, game->context.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texturePixels);

					delete [] texturePixels;
				}
			} break;

			case APP_CMD_TERM_WINDOW:
			{
				int pixelDataSize = game->context.width * game->context.height * 4;

				uint8 * texturePixels = new uint8[pixelDataSize];

				glBindFramebuffer(GL_FRAMEBUFFER, game->canvasFramebuffer);
				glReadPixels(0, 0, game->context.width, game->context.height, GL_RGBA, GL_UNSIGNED_BYTE, texturePixels);

				lseek(game->canvasFile, 0, SEEK_SET);
				int written = write(game->canvasFile, texturePixels, pixelDataSize);

				delete[] texturePixels;

				if (written == pixelDataSize)
				{
					log_info("Canvas file saved fully.");
				}
				else
				{
					__android_log_print(ANDROID_LOG_INFO, "Game", "file not saved = %d, %d", written, errno);
				}

				game->canvasStoredToFile = true;

				terminate_opengl(&game->context);

				// Todo(Leo): thread guard
				game->initialized = false;
				// This is called when we go background
			} break;

			case APP_CMD_DESTROY:
			{
				// This is called when app closes for good
			}break;
		}
	}


	///// POST-PROCESS SOME COMMANDS
	switch (cmd) {
		case APP_CMD_TERM_WINDOW:
			GLUE_LOGV("APP_CMD_TERM_WINDOW\n");
			pthread_mutex_lock(&game->mutex);
			game->window = NULL;
			pthread_cond_broadcast(&game->cond);
			pthread_mutex_unlock(&game->mutex);
			break;

		case APP_CMD_SAVE_STATE:
			GLUE_LOGV("APP_CMD_SAVE_STATE\n");
			pthread_mutex_lock(&game->mutex);
			game->stateSaved = 1;
			pthread_cond_broadcast(&game->cond);
			pthread_mutex_unlock(&game->mutex);
			break;

		case APP_CMD_RESUME:
			free_saved_state(game);
			break;
	}
}

internal void* game_thread_entry(void* param)
{
	Game * game = (Game*)param;

	game->config = AConfiguration_new();
	AConfiguration_fromAssetManager(game->config, game->activity->assetManager);

	print_cur_config(game);

	ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
	ALooper_addFd(looper, game->msgread, LOOPER_ID_MAIN, ALOOPER_EVENT_INPUT, NULL, (void*)process_cmd);
	game->looper = looper;

	pthread_mutex_lock(&game->mutex);
	game->running = true;
	pthread_cond_broadcast(&game->cond);
	pthread_mutex_unlock(&game->mutex);

	// MAIN LOOP
	{
		log_info("Start main");

		// Todo(Leo): We assume that thread will not stop like it should, when we are not drawing
		timespec 	frameFlipTime = time_now();
		float 		elapsedTime = 0;

		while(game->running)
		{

			/// PROCESS ANDROID INPUT AND COMMAND EVENTS
			{
				// Todo(Leo): Check if we would see message type here, and not use user pointer paradigm here
				using ProcessFunc = void(Game*);
				static_assert(std::is_same<decltype(process_input), ProcessFunc>::value, "");
				static_assert(std::is_same<decltype(process_cmd), ProcessFunc>::value, "");

				ProcessFunc * processFunc;

				if (ALooper_pollAll(0, nullptr, nullptr, (void**)&processFunc) >= 0)
				{
					if (processFunc != nullptr)
					{
						processFunc(game);
					}
				}
			}

			auto process_draw_queue = [&]
			{
				game->currentTouchPosition = game->drawPositionQueue[0];
				update_stroke(	game,
								game->lastDequedDrawPosition,
								game->drawPositionQueue[0],
								game->drawPositionQueue[1],
								game->drawPositionQueue[2]);


				game->drawPositionQueueCount -= 1;
				game->lastDequedDrawPosition = game->drawPositionQueue[0];

				for (int i = 0; i < game->drawPositionQueueCount; ++i)
				{
					game->drawPositionQueue[i] = game->drawPositionQueue[i + 1];
				}
			};

			game->drawPositionQueueFlushCount = 3;
			while(game->drawPositionQueueCount > game->drawPositionQueueFlushCount)
			{
				process_draw_queue();
			}

			if (game->drawPositionQueueRefreshed == false && game->drawPositionQueueCount > 0)
			{
				process_draw_queue();
			}

			game->drawPositionQueueRefreshed = false;


			/// UPDATE TRANSITIONS
			{
				// Todo(Leo): I do not like how values like 'menuViewPosition' is used here and defined elswhere
				// while addition/subtraction and comparison is defined here. See if that could be fixed.

				if (game->state == VIEW_TRANSITION_TO_MENU)
				{
					game->viewPosition += elapsedTime / game->viewTransitionDuration;
					if (game->viewPosition > game->menuViewPosition)
					{
						game->viewPosition = game->menuViewPosition;
						game->state = VIEW_MENU;
					}
				}
				else if (game->state == VIEW_TRANSITION_TO_DRAW)
				{
					game->viewPosition -= elapsedTime / game->viewTransitionDuration;
					if (game->viewPosition < game->drawViewPosition)
					{
						game->viewPosition = game->drawViewPosition;
						game->state = VIEW_DRAW;
					}
				}
			}

			draw_canvas(game);
			eglSwapBuffers(game->context.display, game->context.surface);

			// Todo(Leo): there is small distortion here, since time_elapsed_seconds gets its
			// own 'time_now()' slighlty before new frameFlipTime's 'time_now()'
			elapsedTime 	= time_elapsed_seconds(frameFlipTime);
			frameFlipTime 	= time_now();
		}

		log_info("Finish main");
	}

	// Todo(Leo): Think through if this is right place to destroy this app, because we don't actually create game in this scope
	GLUE_LOGV("android_app_destroy!");
	free_saved_state(game);
	pthread_mutex_lock(&game->mutex);
	if (game->inputQueue != NULL) {
		AInputQueue_detachLooper(game->inputQueue);
	}
	AConfiguration_delete(game->config);
	game->destroyed = 1;
	pthread_cond_broadcast(&game->cond);
	pthread_mutex_unlock(&game->mutex);
	// Can't touch game object after this.

	log_info("Exit game thread!");

	close(game->canvasFile);

	return NULL;
}


internal void android_app_set_window(Game * game, ANativeWindow* window)
{
	pthread_mutex_lock(&game->mutex);
	if (game->pendingWindow != NULL)
	{
		android_app_write_cmd(game, APP_CMD_TERM_WINDOW);
	}
	
	game->pendingWindow = window;
	if (window != NULL)
	{
		android_app_write_cmd(game, APP_CMD_INIT_WINDOW);
	}
	while (game->window != game->pendingWindow) {
		pthread_cond_wait(&game->cond, &game->mutex);
	}
	pthread_mutex_unlock(&game->mutex);
}

////////////////////////////////////////////////////////////////////////////////////////////
////			ANDROID CALLBACKS AND DRAGONS AND SHARKS 								////
////////////////////////////////////////////////////////////////////////////////////////////

internal void android_callback_onDestroy(ANativeActivity* activity)
{
	GLUE_LOGV("Destroy: %p\n", activity);

	Game * game = (Game*)activity->instance;

	pthread_mutex_lock(&game->mutex);
	android_app_write_cmd(game, APP_CMD_DESTROY);
	while (!game->destroyed) {
		pthread_cond_wait(&game->cond, &game->mutex);
	}
	pthread_mutex_unlock(&game->mutex);

	close(game->msgread);
	close(game->msgwrite);
	pthread_cond_destroy(&game->cond);
	pthread_mutex_destroy(&game->mutex);

	delete game;
}

internal void android_callback_onStart(ANativeActivity* activity)
{
	log_info("onStart");
}

internal void android_callback_onResume(ANativeActivity* activity)
{
	log_info("onResume");
}

internal void * android_callback_onSaveInstanceState(ANativeActivity* activity, size_t* outLen)
{
	Game * game = (Game*)activity->instance;
	void* savedState = NULL;

	GLUE_LOGV("SaveInstanceState: %p\n", activity);
	pthread_mutex_lock(&game->mutex);
	game->stateSaved = 0;
	android_app_write_cmd(game, APP_CMD_SAVE_STATE);
	while (!game->stateSaved) {
		pthread_cond_wait(&game->cond, &game->mutex);
	}

	if (game->savedState != NULL) {
		savedState = game->savedState;
		*outLen = game->savedStateSize;
		game->savedState = NULL;
		game->savedStateSize = 0;
	}

	pthread_mutex_unlock(&game->mutex);

	return savedState;
}

internal void android_callback_onPause(ANativeActivity* activity)
{
	log_info("onPause");
}

internal void android_callback_onStop(ANativeActivity* activity)
{
	log_info("onStop");
}

internal void android_callback_onConfigurationChanged(ANativeActivity* activity)
{
	Game * game = (Game*)activity->instance;
	GLUE_LOGV("ConfigurationChanged: %p\n", activity);
	android_app_write_cmd(game, APP_CMD_CONFIG_CHANGED);
}

internal void android_callback_onLowMemory(ANativeActivity* activity)
{
	Game * game = (Game*)activity->instance;
	GLUE_LOGV("LowMemory: %p\n", activity);
	android_app_write_cmd(game, APP_CMD_LOW_MEMORY);
}

internal void android_callback_onWindowFocusChanged(ANativeActivity* activity, int focused)
{
	GLUE_LOGV("WindowFocusChanged: %p -- %d\n", activity, focused);
	android_app_write_cmd((Game*)activity->instance,
			focused ? APP_CMD_GAINED_FOCUS : APP_CMD_LOST_FOCUS);
}

internal void android_callback_onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window)
{
	GLUE_LOGV("NativeWindowCreated: %p -- %p\n", activity, window);
	android_app_set_window((Game*)activity->instance, window);
}

internal void android_callback_onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window)
{
	GLUE_LOGV("NativeWindowDestroyed: %p -- %p\n", activity, window);
	android_app_set_window((Game*)activity->instance, NULL);
}

internal void android_callback_onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue)
{
	Game * game = (Game*)activity->instance;

	// Study
	// Note(Leo): This seems that we block here until we have processed message elsewhere
	// Same on below

	pthread_mutex_lock(&game->mutex);
	game->pendingInputQueue = queue;
	android_app_write_cmd(game, APP_CMD_INPUT_CHANGED);
	while (game->inputQueue != game->pendingInputQueue)
	{
		pthread_cond_wait(&game->cond, &game->mutex);
	}
	pthread_mutex_unlock(&game->mutex);

}

internal void android_callback_onInputQueueDestroyed(ANativeActivity* activity, AInputQueue*)
{
	Game * game = (Game*)activity->instance;

	pthread_mutex_lock(&game->mutex);
	game->pendingInputQueue = nullptr;
	android_app_write_cmd(game, APP_CMD_INPUT_CHANGED);
	while (game->inputQueue != game->pendingInputQueue)
	{
		pthread_cond_wait(&game->cond, &game->mutex);
	}
	pthread_mutex_unlock(&game->mutex);
}


JNIEXPORT
void ANativeActivity_onCreate(	ANativeActivity* activity,
								void* savedState,
								size_t savedStateSize)
{
	GLUE_LOGV("Creating: %p\n", activity);
	activity->callbacks->onDestroy 						= android_callback_onDestroy;
	activity->callbacks->onStart 						= android_callback_onStart;
	activity->callbacks->onResume 						= android_callback_onResume;
	activity->callbacks->onSaveInstanceState 			= android_callback_onSaveInstanceState;
	activity->callbacks->onPause 						= android_callback_onPause;
	activity->callbacks->onStop 						= android_callback_onStop;
	activity->callbacks->onConfigurationChanged 		= android_callback_onConfigurationChanged;
	activity->callbacks->onLowMemory 					= android_callback_onLowMemory;
	activity->callbacks->onWindowFocusChanged 			= android_callback_onWindowFocusChanged;
	activity->callbacks->onNativeWindowCreated 			= android_callback_onNativeWindowCreated;
	activity->callbacks->onNativeWindowDestroyed 		= android_callback_onNativeWindowDestroyed;
	activity->callbacks->onInputQueueCreated 			= android_callback_onInputQueueCreated;
	activity->callbacks->onInputQueueDestroyed 			= android_callback_onInputQueueDestroyed;

	{
		// Note(Leo): yes, this is actually me allocating with new :)
		// Todo(Leo): Delete is somewhere stupidly, so do something about that
		Game * game = new Game();

		// Todo(Leo): This seems stupid since now game needs to reference activity and activity
		// needs to reference game
		game->activity = activity;

		pthread_mutex_init(&game->mutex, NULL);
		pthread_cond_init(&game->cond, NULL);

		if (savedState != NULL) {
			game->savedState = malloc(savedStateSize);
			game->savedStateSize = savedStateSize;
			memcpy(game->savedState, savedState, savedStateSize);
		}

		{
			void * pixels;
			int pixelsSize = 720 * 1280 * 4;

			int file = open(activity->internalDataPath, O_RDWR | O_TMPFILE);

			if (file == -1)
			{
				__android_log_print(ANDROID_LOG_INFO, "Game", "NOT opened canvas file, error = %d", errno);
			}
			else
			{
				game->canvasFile = file;
				__android_log_print(ANDROID_LOG_INFO, "Game", "opened canvas file = %d", file);
			}
		}

		int msgpipe[2];
		if (pipe(msgpipe)) {
			__android_log_print(ANDROID_LOG_ERROR, "Game", "could not create pipe: %s", strerror(errno));
			activity->instance = nullptr;

			// Todo(Leo): also do something else???
		}
		else
		{
			game->msgread = msgpipe[0];
			game->msgwrite = msgpipe[1];

			pthread_attr_t attr; 
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			pthread_create(&game->thread, &attr, game_thread_entry, game);

			// Wait for thread to start.
			pthread_mutex_lock(&game->mutex);
			while (game->running == false) { 
				pthread_cond_wait(&game->cond, &game->mutex);
			}
			pthread_mutex_unlock(&game->mutex);

			activity->instance = game;
		}
	}
}
