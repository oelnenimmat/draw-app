/*
Leo Tamminen

Resources:
	https://github.com/android/ndk-samples/blob/master/native-activity/app/src/main/cpp/main.cpp
	https://community.arm.com/developer/tools-software/oss-platforms/b/android-blog/posts/check-your-context-if-glcreateshader-returns-0-and-gl_5f00_invalid_5f00_operation
*/

#include <android/log.h>
#include <android/native_activity.h>

#include <unistd.h>
#include <pthread.h>

// #include <android_native_app_glue.h>
#include <jni.h>
#include <cassert>
#include <time.h>

// Note(Leo): for dynamic loading, android only??
// #include <dlfcn.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
// #include <memory>

#include <cmath>
// #include <vector>

using bool32 = __int32_t;
using int32 = __int32_t;

// using BinaryAsset = std::vector<char>;

#define internal static

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

// char const * android_app_cmd_string(int32_t cmd)
// {
// 	switch(cmd)
// 	{
// 		case APP_CMD_INPUT_CHANGED: 		return "APP_CMD_INPUT_CHANGED";
// 		case APP_CMD_INIT_WINDOW: 			return "APP_CMD_INIT_WINDOW";
// 		case APP_CMD_TERM_WINDOW: 			return "APP_CMD_TERM_WINDOW";
// 		case APP_CMD_WINDOW_RESIZED: 		return "APP_CMD_WINDOW_RESIZED";
// 		case APP_CMD_WINDOW_REDRAW_NEEDED: 	return "APP_CMD_WINDOW_REDRAW_NEEDED";
// 		case APP_CMD_CONTENT_RECT_CHANGED: 	return "APP_CMD_CONTENT_RECT_CHANGED";
// 		case APP_CMD_GAINED_FOCUS: 			return "APP_CMD_GAINED_FOCUS";
// 		case APP_CMD_LOST_FOCUS: 			return "APP_CMD_LOST_FOCUS";
// 		case APP_CMD_CONFIG_CHANGED: 		return "APP_CMD_CONFIG_CHANGED";
// 		case APP_CMD_LOW_MEMORY: 			return "APP_CMD_LOW_MEMORY";
// 		case APP_CMD_START: 				return "APP_CMD_START";
// 		case APP_CMD_RESUME: 				return "APP_CMD_RESUME";
// 		case APP_CMD_SAVE_STATE: 			return "APP_CMD_SAVE_STATE";
// 		case APP_CMD_PAUSE: 				return "APP_CMD_PAUSE";
// 		case APP_CMD_STOP: 					return "APP_CMD_STOP";
// 		case APP_CMD_DESTROY: 				return "APP_CMD_DESTROY";

// 		default:
// 			return "Unknown android_app command!";
// 	}
// }

internal void log_info(char const * message)
{
	__android_log_write(ANDROID_LOG_INFO, "Game", message);
}

internal void log_error(char const * message)
{
	__android_log_write(ANDROID_LOG_ERROR, "Game", message);
}



struct GLContext
{
	bool32 isGood;

	EGLDisplay display;
	EGLSurface surface;
	EGLContext eglContext;

	AAssetManager * assetManager;

	int32 width;
	int32 height;
	float ratio () { return (float)width / height; }
};

GLContext InitializeOpenGL(ANativeWindow * window)
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

	glEnable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	log_info("Done initializing opengl");
	return context;
}

struct v2
{
	float x, y;
};

static v2 operator + (v2 a, v2 b)
{
	v2 result = {a.x + b.x, a.y + b.y};
	return result;
}

static v2 operator - (v2 a, v2 b)
{
	v2 result = {a.x - b.x, a.y - b.y};
	return result;
}

static float v2_magnitude(v2 v)
{
	float result = std::sqrt(v.x * v.x + v.y * v.y);
	return result;
}

struct v3
{
	float r, g, b;
};

static v3 v3_lerp(v3 a, v3 b, float t)
{
	float omt = 1 - t;

	a.r = omt * a.r + t * b.r;
	a.g = omt * a.g + t * b.g;
	a.b = omt * a.b + t * b.b;

	return a;
}

static float float_clamp(float value, float min, float max)
{
	if (value < min)
		value = min;
	else if (value > max)
		value = max;
	return value;
}

static float float_lerp(float a, float b, float t)
{
	a = (1 - t) * a + t * b;
	return a;
}

struct Game
{
	// bool32 initialized = false;
	bool32 running;
	bool32 destroyRequested;

	GLContext context;

	GLuint brushShaderId;
	GLuint brushMaskTextureId;
	GLuint brushGradientTextureId;

	GLuint canvasShaderId;
	GLuint canvasTextureId;
	GLuint canvasFramebuffer;

	v2 			lastTouchPosition;
	float 		lastStrokeLength;

	timespec 	touchDownTime;
	bool 		strokeMoved;
	float 		strokeWidth;

	// Main loop will be run in separate thread, apparently standard android thing
	pthread_t 		thread;
	pthread_mutex_t mutex;
	pthread_cond_t 	cond;


};

const char * brushVertexShaderSource =
R"(	#version 300 es
	in vec4 position;

	uniform mat4 projection;
	uniform mat4 view;
	uniform mat4 model;

	out vec2 uv;

	void main()
	{
		gl_Position = projection * view * model * position;
		uv = position.xy;
	}
)";

const char * brushFragmentShaderSource =
R"(	#version 300 es
	precision mediump float;

	in vec2 uv;

	uniform sampler2D _texture;
	uniform sampler2D 	gradientColor;
	uniform float 		gradientPosition;
	
	uniform vec3 color;

	out vec4 fragColor;
	void main()
	{
		float len = length(uv) * 2.0;

		// float alpha = step(len, 1.0);
		// float alpha = clamp(1.0 - len, 0.0, 1.0);
		float alpha = 1.0 - smoothstep(0.5, 1.0, len);

		vec4 color_ = texture(gradientColor, vec2(gradientPosition, 0));

		fragColor = vec4(color_.rgb, alpha);
	}
)";

const char * canvasVertexShaderSource =
R"(	#version 300 es
	in vec4 position;

	out vec2 uv;
	void main()
	{
		gl_Position = vec4(position.xy, 0, 1);
		uv 			= position.zw;
	}
)";

const char * canvasFragmentShaderSource =
R"(	#version 300 es
	precision mediump float;

	in vec2 uv;

	uniform sampler2D _texture;

	out vec4 fragColor;
	void main()
	{
		fragColor = texture(_texture, uv);
	}
)";

GLuint load_shader(const char * source, GLenum type)
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
}

void log_gl_shader_program(GLuint program)
{
	GLsizei length;
	char buffer [1024];
	glGetProgramInfoLog(program, 1024, &length, buffer);

	__android_log_print(ANDROID_LOG_INFO, "Game", "Shader program log (%d): %s", program, buffer);
}

// BinaryAsset LoadTestTexture(AAssetManager * manager, const char * path)
// {
//     AAsset * file = AAssetManager_open(manager, path, AASSET_MODE_BUFFER);
//     size_t fileSize = AAsset_getLength(file);

//     BinaryAsset result (fileSize);
//     AAsset_read(file, result.data(), fileSize);
//     return result;
// }

void initialize_shaders(Game * game)
{
	constexpr unsigned char F = 255;

	{
		GLuint brushVertexShader = load_shader(brushVertexShaderSource, GL_VERTEX_SHADER);
		GLuint brushFragmentShader = load_shader(brushFragmentShaderSource, GL_FRAGMENT_SHADER);

		// Todo(Leo): This can fail
		game->brushShaderId = glCreateProgram();

		glAttachShader(game->brushShaderId, brushVertexShader);
		glAttachShader(game->brushShaderId, brushFragmentShader);
		glLinkProgram(game->brushShaderId);

		// unsigned char brushTexturePixels [] =
		// {
		// 	0,0,0,F, 0,0,0,F, 0,0,0,F, 0,0,0,F,
		// 	0,0,0,F, F,F,F,F, F,F,F,F, 0,0,0,F,
		// 	0,0,0,F, F,F,F,F, F,F,F,F, 0,0,0,F,
		// 	0,0,0,F, 0,0,0,F, 0,0,0,F, 0,0,0,F,
		// };

		unsigned int brushTexturePixels [] =
		{
			0x00000000, 0x00000000, 0x00000000,
			0x00000000, 0xFFFFFFFF, 0x00000000,
			0x00000000, 0x00000000, 0x00000000,
		};

		GLuint brushTexture;
		glGenTextures(1, &brushTexture);
		glBindTexture(GL_TEXTURE_2D, brushTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, brushTexturePixels);

		game->brushMaskTextureId = brushTexture;

		unsigned char brushGradientTexturePixels [] =
		{
			204, 38, 0, 255,
			204, 38, 0, 255,
			// 102, 134, 127, 255,
			255, 230, 200, 255,
			0, 230, 255, 255,
			0, 230, 255, 255,
			0, 230, 255, 255,
		};
		int gradientPixelCount = sizeof(brushGradientTexturePixels) / 4;

		GLuint brushGradientTexture;
		glGenTextures(1, &brushGradientTexture);
		glBindTexture(GL_TEXTURE_2D, brushGradientTexture);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gradientPixelCount, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, brushGradientTexturePixels);

		game->brushGradientTextureId = brushGradientTexture;
	}

	/// Canvas....
	{
		GLuint canvasVertexShader = load_shader(canvasVertexShaderSource, GL_VERTEX_SHADER);
		GLuint canvasFragmentShader = load_shader(canvasFragmentShaderSource, GL_FRAGMENT_SHADER);

		game->canvasShaderId = glCreateProgram();
		glAttachShader(game->canvasShaderId, canvasVertexShader);
		glAttachShader(game->canvasShaderId, canvasFragmentShader);
		glLinkProgram(game->canvasShaderId);

		// glUseProgram(game->canvasShaderId);

		GLuint canvasTexture;
		glGenTextures(1, &canvasTexture);
		glBindTexture(GL_TEXTURE_2D, canvasTexture);

		int screenWidth = game->context.width;
		int screenHeight = game->context.height;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		int pixelCount = screenWidth * screenHeight;
		int memorySize = pixelCount * 4;
		unsigned char * canvasTextureMemory = new unsigned char [memorySize];
		// memset(canvasTextureMemory, F, memorySize);
		for (int i = 0; i < memorySize; i += 4)
		{
			canvasTextureMemory[i] = 255;
			canvasTextureMemory[i + 1] = 255;
			canvasTextureMemory[i + 2] = 255;
			canvasTextureMemory[i + 3] = 255;
		}


		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, screenWidth, screenHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, canvasTextureMemory);

		delete[] canvasTextureMemory;

		game->canvasTextureId = canvasTexture;

		glGenFramebuffers(1, &game->canvasFramebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, game->canvasFramebuffer);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, game->canvasTextureId, 0);

		// assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

		log_gl_shader_program(game->canvasShaderId);

		__android_log_print(ANDROID_LOG_INFO, "Game", "Framebuffer Status = %s", gl_framebuffer_status_string(glCheckFramebufferStatus(GL_FRAMEBUFFER)));
	// }


		// Note(Leo): Im not sure about these, this is unbinding
	//    glActiveTexture(0);
	//    glBindTexture(GL_TEXTURE_2D, 0);

	}

	__android_log_print(ANDROID_LOG_INFO, "Game", "Canvas shader(%d), brush shader (%d)", game->canvasShaderId, game->brushShaderId);
}

void draw_brush(Game * game, v2 screenPosition, float size, float gradientPosition)
{
	GLfloat vertices [] =
	{
		-0.5, -0.5,
		 0.5, -0.5,
		-0.5,  0.5,
		 0.5,  0.5,
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
	// float size = 50;

	GLfloat model [] =
	{
		size, 0, 0, 0,
		0, size, 0, 0,
		0, 0, 1, 0,
		x, y, 0, 1,
	};

	glUseProgram(game->brushShaderId);

	GLint projectionLocation 		= glGetUniformLocation(game->brushShaderId, "projection");
	GLint viewLocation				= glGetUniformLocation(game->brushShaderId, "view");
	GLint modelMatrixLocation 		= glGetUniformLocation(game->brushShaderId, "model");
	GLint textureLocation 			= glGetUniformLocation(game->brushShaderId, "_texture");
	GLint colorLocation				= glGetUniformLocation(game->brushShaderId, "color");
	GLint gradientTextureLocation 	= glGetUniformLocation(game->brushShaderId, "gradientColor");
	GLint gradientPositionLocation 	= glGetUniformLocation(game->brushShaderId, "gradientPosition");


	// Bind canvas framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, game->canvasFramebuffer);
	glViewport(0, 0, game->context.width, game->context.height);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
	glEnableVertexAttribArray(0);

	glUniformMatrix4fv(projectionLocation, 1, false, projection);
	glUniformMatrix4fv(viewLocation, 1, false, view);
	glUniformMatrix4fv(modelMatrixLocation, 1, false, model);

	glUniform1i(textureLocation, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, game->brushMaskTextureId);

	glUniform1i(gradientTextureLocation, 1);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, game->brushGradientTextureId);

	glUniform1f(gradientPositionLocation, gradientPosition);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable( GL_BLEND );

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(0);

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


void draw_canvas(Game * game)
{
	GLfloat canvasVertices [] =
	{
		-1, -1, 0, 0,
		 3, -1, 2, 0,
		-1,  3, 0, 2,
	};

	// Bind screen framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, game->context.width, game->context.height);

	glClearColor(0.7f, 0.2f, 0.2f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(game->canvasShaderId);
	glDisable( GL_BLEND );

	GLint textureLocation 	= glGetUniformLocation(game->canvasShaderId, "_textureA");

	glUniform1i(textureLocation, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, game->canvasTextureId);

	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, canvasVertices);
	glEnableVertexAttribArray(0);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

	// Unbind so we can draw this on next frame
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisableVertexAttribArray(0);


	// Done

	eglSwapBuffers(game->context.display, game->context.surface);
}

extern "C"
{
// 	int32 handle_input (android_app* application, AInputEvent * event)
// 	{
// 		Game * game = static_cast<Game*>(application->userData);
// 		bool32 handled = false;
// 		switch (AInputEvent_getType(event))
// 		{
// 			case AINPUT_EVENT_TYPE_MOTION:
// 			{
// 				constexpr float minBrushSize 		= 25;
// 				constexpr float maxBrushSize 		= 75;
// 				constexpr float maxBrushSizeTimeMS 	= 500;


// 				switch (AMotionEvent_getAction(event))
// 				{
// 					case AMOTION_EVENT_ACTION_DOWN:
// 					{
// 						game->lastTouchPosition = {AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0)};

// 						game->touchDownTime 	= time_now();
// 						game->strokeMoved 		= false;
// 						game->lastStrokeLength 	= 0;

// 					} break;

// 					case AMOTION_EVENT_ACTION_UP:
// 					{
// 						if (game->strokeMoved == false)
// 						{
// 							float timeSinceTouchDownMS 	= time_elapsed_milliseconds(game->touchDownTime);
// 							float interpolatonTime 		= float_clamp(timeSinceTouchDownMS / maxBrushSizeTimeMS, 0, 1);
// 							float strokeWidth 			= float_lerp(minBrushSize, maxBrushSize, interpolatonTime);

// 							draw_brush(game, game->lastTouchPosition, strokeWidth, 0);
// 						}
// 					} break;

// 					case AMOTION_EVENT_ACTION_MOVE:
// 					{
// 						constexpr float maxStrokeLength 			= 80;
// 						constexpr float strokeStartMoveThreshold 	= 10;

// 						v2 currentTouchPosition = {AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0)};
// 						v2 stroke 				= currentTouchPosition - game->lastTouchPosition;
// 						float strokeLength 		= v2_magnitude(stroke);

// 						if (game->strokeMoved == false)
// 						{
// 							if (strokeLength >= strokeStartMoveThreshold)
// 							{
// 								float timeSinceTouchDownMS 	= time_elapsed_milliseconds(game->touchDownTime);
// 								float interpolatonTime 		= float_clamp(timeSinceTouchDownMS / maxBrushSizeTimeMS, 0, 1);
// 								game->strokeWidth 			= float_lerp(minBrushSize, maxBrushSize, interpolatonTime);
// 								game->strokeMoved 			= true;
// 								game->lastStrokeLength 		= strokeLength;
// 							}
// 							else
// 							{
// 								break;
// 							}
// 						}

// 						/*
// 						Todo(Leo): Currently we track lenght of stroke for color, but we would like to track
// 						speed, so use time stuff for that.
// 						*/

// 					 	float drawStrokeLengthThreshold = game->strokeWidth / 10;
// 						int dotCount 					= std::floor(strokeLength / drawStrokeLengthThreshold);

// 						v2 dotPosition 			= game->lastTouchPosition;
// 						v2 dotPositionStep 		= {stroke.x / dotCount, stroke.y / dotCount};

// 						strokeLength 			= float_lerp(game->lastStrokeLength, strokeLength, 0.1);

// 						float strokeLengthDelta = strokeLength - game->lastStrokeLength;
// 						float strokeLengthStep 	= strokeLengthDelta / dotCount;
// 						float strokeLengthState = game->lastStrokeLength;

// 						v3 darkColor = {0.8, 0.15, 0.0};
// 						v3 lightColor = {0.0, 0.9, 1.0}; // 0x00dbff

// 						// v3 darkColor = {0.2, 0.1, 0.1};
// 						// v3 lightColor = {1.0, 0.2, 0.4};

// 						for (int i = 0; i < dotCount; ++i)
// 						{
// 							float colorInterpolationTime 	= float_clamp(strokeLengthState / maxStrokeLength, 0, 1);
// 							v3 color 						= v3_lerp(darkColor, lightColor, colorInterpolationTime);

// 							draw_brush(game, dotPosition, game->strokeWidth, colorInterpolationTime);

// 							dotPosition 		= dotPosition + dotPositionStep;
// 							strokeLengthState 	+= strokeLengthStep;
// 						}

// 						float colorInterpolationTime 	= float_clamp(strokeLength / maxStrokeLength, 0, 1);
// 						v3 color 						= v3_lerp(darkColor, lightColor, colorInterpolationTime);

// 						draw_brush(game, currentTouchPosition, game->strokeWidth, colorInterpolationTime);

// 						game->lastTouchPosition = currentTouchPosition;
// 						game->lastStrokeLength = strokeLength;


// 						handled = true;
// 					} break;
// 				}
// 			} break;

// 			case AINPUT_EVENT_TYPE_KEY:
// 			{

// 			} break;
// 		}

// 		return handled;
// 	}

// 	void handle_cmd(android_app * application, int32_t cmd)
// 	{
// 		__android_log_print(ANDROID_LOG_INFO, "Game", "android_app cmd: %s", android_app_cmd_string(cmd));

// 		Game * game = static_cast<Game*>(application->userData);
// 		switch (cmd)
// 		{
// 			case APP_CMD_INIT_WINDOW:
// 			{
// 				if (game->initialized == false)
// 				{
// 					game->initialized = true;
// 					game->context = InitializeOpenGL(application->window);
// 					initialize_shaders (game);

// 					draw_canvas(game);
// 				}

// //                vulkanInstance = InitializeVulkan(application->window);
// //                log_info("Vulkan created");
// 			} break;

// 			case APP_CMD_TERM_WINDOW:
// 			{
// //                TerminateVulkan(vulkanInstance);
// //                log_info("Vulkan terminated");
// 			} break;

// 			case APP_CMD_DESTROY:
// 			{
// //                log_info("Application destroyed");
// 			}break;
// 		}
// 	}

// 	void android_main(android_app * application)
// 	{
// 		log_info("Start main");

// 		Game game = {};

// 		application->onAppCmd       = handle_cmd;
// 		application->onInputEvent   = handle_input;
// 		application->userData       = &game;

// 		int events;
// 		android_poll_source * source;
		
// 		while(!application->destroyRequested)
// 		{
// 			if (ALooper_pollAll(0, nullptr, &events, (void**)&source) >= 0)
// 			{
// 				if (source != nullptr)
// 				{
// 					source->process(application, source);
// 				}
// 			}

// 			draw_canvas(&game);
// 		}

// 		// Note(Leo): This does not show in logcat. Something to do with android_app::destroyRequested??
// 		log_info("Finish main");

// 	}
}

internal void android_on_start(ANativeActivity * activity)
{
	log_info("Hello from start");


	log_info("Start ok!");
}

internal void android_on_native_window_created(ANativeActivity * activity, ANativeWindow * window)
{
	Game * game = (Game*)activity->instance;

	game->context = InitializeOpenGL(window);
	initialize_shaders(game);

	log_info("Native window created");
}

internal void android_on_native_window_destroyed(ANativeActivity * activity, ANativeWindow * window)
{
	log_info("Native window destroyed");
}

internal void android_on_destroy(ANativeActivity * activity)
{
	Game * game 		= (Game*)activity->instance;

	pthread_mutex_lock(&game->mutex);
	game->running = false;
	pthread_mutex_unlock(&game->mutex);

	log_info("Destroy requested");
}

internal void * game_thread(void * param)
{
	log_info("Hello from thread");

	Game * game = (Game*)param;

	// Note(Leo): This may not be necessary, int values should be atomic
	pthread_mutex_lock(&game->mutex);
	game->running = true;
	pthread_mutex_unlock(&game->mutex);


	log_info("thread loop start");

	// sleep(5);
	// int loopCount = 1000;
	// while(loopCount--)
	while(game->running)
	{
		draw_canvas(game);
	}

	pthread_mutex_destroy(&game->mutex);
	pthread_cond_destroy(&game->cond);

	// Todo(Leo): do this, but we dont have access to it yet...
	// activity->instance 	= nullptr;
	delete game;

	log_info("Deleted game");


	log_info("Thread done!");

	// Study(Leo): Why do we have to return null ptr
	return nullptr;
}

JNIEXPORT
void ANativeActivity_onCreate(	ANativeActivity* activity,
								void* savedState,
                              	size_t savedStateSize)
{
 	log_info("Hello");

 	activity->callbacks->onStart 	= android_on_start;
 	activity->callbacks->onDestroy 	= android_on_destroy;

 	activity->callbacks->onNativeWindowCreated 		= android_on_native_window_created;
 	activity->callbacks->onNativeWindowDestroyed 	= android_on_native_window_destroyed;

	Game * game 		= new Game;
	activity->instance 	= game;

 	pthread_mutex_init(&game->mutex, nullptr);
 	pthread_cond_init(&game->cond, nullptr);

 	pthread_attr_t threadAttributes;
 	pthread_attr_init(&threadAttributes);
 	pthread_attr_setdetachstate(&threadAttributes, PTHREAD_CREATE_DETACHED);
 	pthread_create(&game->thread, &threadAttributes, game_thread, game);

 	pthread_mutex_lock(&game->mutex);
 	while(game->running == false)
 	{
 		pthread_cond_wait(&game->cond, &game->mutex);
 	}
 	pthread_mutex_unlock(&game->mutex);


    // LOGV("Creating: %p\n", activity);
    // activity->callbacks->onDestroy = onDestroy;
    // activity->callbacks->onStart = onStart;
    // activity->callbacks->onResume = onResume;
    // activity->callbacks->onSaveInstanceState = onSaveInstanceState;
    // activity->callbacks->onPause = onPause;
    // activity->callbacks->onStop = onStop;
    // activity->callbacks->onConfigurationChanged = onConfigurationChanged;
    // activity->callbacks->onLowMemory = onLowMemory;
    // activity->callbacks->onWindowFocusChanged = onWindowFocusChanged;
    // activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    // activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    // activity->callbacks->onInputQueueCreated = onInputQueueCreated;
    // activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;

    // activity->instance = android_app_create(activity, savedState, savedStateSize);
}
