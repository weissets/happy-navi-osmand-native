#ifdef ANDROID_BUILD
#include <dlfcn.h>
#endif
#include <SkBitmap.h>
#include <SkCanvas.h>
#include <SkImageDecoder.h>
#include <SkImageEncoder.h>
#include <SkStream.h>
#include "java_renderRules.h"
#include "Common.h"
#include "java_wrap.h"
#include "binaryRead.h"
#include "rendering.h"
#include "binaryRoutePlanner.h"
#include "Logging.h"

JavaVM* globalJVM = NULL;
void loadJniRenderingContext(JNIEnv* env);
void loadJniRenderingRules(JNIEnv* env);
jclass jclassIntArray ;
jclass jclassString;
jclass jclassStringArray;
jmethodID jmethod_Object_toString = NULL;


extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
	JNIEnv* globalJniEnv;
	if(vm->GetEnv((void **)&globalJniEnv, JNI_VERSION_1_6))
		return JNI_ERR; /* JNI version not supported */
	globalJVM = vm;
	loadJniRenderingContext(globalJniEnv);
	loadJniRenderingRules(globalJniEnv);
	jclassIntArray = findGlobalClass(globalJniEnv, "[I");
	jclassStringArray = findGlobalClass(globalJniEnv, "[Ljava/lang/String;");
	jclassString = findGlobalClass(globalJniEnv, "java/lang/String");

	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "JNI_OnLoad completed");
	
	return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL Java_net_osmand_NativeLibrary_deleteSearchResult(JNIEnv* ienv,
		jobject obj, jlong searchResult) {
	ResultPublisher* result = (ResultPublisher*) searchResult;
	if(result != NULL){
		delete result;
	}
}

extern "C" JNIEXPORT void JNICALL Java_net_osmand_NativeLibrary_closeBinaryMapFile(JNIEnv* ienv,
		jobject obj, jobject path) {
	const char* utf = ienv->GetStringUTFChars((jstring) path, NULL);
	std::string inputName(utf);
	ienv->ReleaseStringUTFChars((jstring) path, utf);
	closeBinaryMapFile(inputName);
}


extern "C" JNIEXPORT jboolean JNICALL Java_net_osmand_NativeLibrary_initCacheMapFiles(JNIEnv* ienv,
		jobject obj,
		jobject path) {
	const char* utf = ienv->GetStringUTFChars((jstring) path, NULL);
	std::string inputName(utf);
	ienv->ReleaseStringUTFChars((jstring) path, utf);
	return initMapFilesFromCache(inputName);
}

extern "C" JNIEXPORT jboolean JNICALL Java_net_osmand_NativeLibrary_initBinaryMapFile(JNIEnv* ienv,
		jobject obj, jobject path) {
	// Verify that the version of the library that we linked against is
	const char* utf = ienv->GetStringUTFChars((jstring) path, NULL);
	std::string inputName(utf);
	ienv->ReleaseStringUTFChars((jstring) path, utf);
	BinaryMapFile* fl = initBinaryMapFile(inputName);
	if(fl == NULL) {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Warning, "File %s was not initialized", inputName.c_str());
	}
	return fl != NULL;
}


extern "C" JNIEXPORT jboolean JNICALL Java_net_osmand_NativeLibrary_initFontType(JNIEnv* ienv,
		jobject obj, jbyteArray byteData, jstring name, jboolean bold, jboolean italic) {
	
	std::string fName = getString(ienv, name);
	jbyte* be = ienv->GetByteArrayElements(byteData, NULL);
	jsize sz = ienv->GetArrayLength(byteData);
	globalFontRegistry.registerStream((const char*)be, sz, fName, bold, italic);
		
	ienv->ReleaseByteArrayElements(byteData, be, JNI_ABORT);
	ienv->DeleteLocalRef(byteData);
	return true;
}


// Global object
UNORDERED(map)<std::string, RenderingRulesStorage*> cachedStorages;

RenderingRulesStorage* getStorage(JNIEnv* env, jobject storage) {
	std::string hash = getStringMethod(env, storage, jmethod_Object_toString);
	if (cachedStorages.find(hash) == cachedStorages.end()) {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Debug, "Init rendering storage %s ", hash.c_str());
		cachedStorages[hash] = createRenderingRulesStorage(env, storage);
	}
	return cachedStorages[hash];
}


extern "C" JNIEXPORT void JNICALL Java_net_osmand_NativeLibrary_initRenderingRulesStorage(JNIEnv* ienv,
		jobject obj, jobject storage) {
	getStorage(ienv, storage);
}

RenderingRuleSearchRequest* initSearchRequest(JNIEnv* env, jobject renderingRuleSearchRequest) {
	jobject storage = env->GetObjectField(renderingRuleSearchRequest, RenderingRuleSearchRequest_storage);
	RenderingRulesStorage* st = getStorage(env, storage);
	env->DeleteLocalRef(storage);
	RenderingRuleSearchRequest* res = new RenderingRuleSearchRequest(st);
	initRenderingRuleSearchRequest(env, res, renderingRuleSearchRequest);
	return res;
}


extern "C" JNIEXPORT jlong JNICALL Java_net_osmand_NativeLibrary_searchNativeObjectsForRendering(JNIEnv* ienv,
		jobject obj, jint sleft, jint sright, jint stop, jint sbottom, jint zoom,
		jobject renderingRuleSearchRequest, bool skipDuplicates,
		int renderRouteDataFile , // deprecated parameter
		jobject objInterrupted, jstring msgNothingFound) {
	RenderingRuleSearchRequest* req = initSearchRequest(ienv, renderingRuleSearchRequest);
	jfieldID interruptedField = 0;
	if(objInterrupted != NULL) {
		jclass clObjInterrupted = ienv->GetObjectClass(objInterrupted);
		interruptedField = getFid(ienv, clObjInterrupted, "interrupted", "Z");
		ienv->DeleteLocalRef(clObjInterrupted);
	}
	jfieldID renderedStateField = 0;
	int renderedState = 0;
	if(objInterrupted != NULL) {
		jclass clObjInterrupted = ienv->GetObjectClass(objInterrupted);
		renderedStateField = getFid(ienv, clObjInterrupted, "renderedState", "I");
		ienv->DeleteLocalRef(clObjInterrupted);
	}

	ResultJNIPublisher* j = new ResultJNIPublisher( objInterrupted, interruptedField, ienv);
	SearchQuery q(sleft, sright, stop, sbottom, req, j);
	q.zoom = zoom;


	/*ResultPublisher* res =*/ searchObjectsForRendering(&q, skipDuplicates, getString(ienv, msgNothingFound), renderedState);
	if(objInterrupted != NULL) {
		ienv->SetIntField(objInterrupted, renderedStateField, renderedState);
	}
	
	delete req;
	return (jlong) j;
}


//////////////////////////////////////////
///////////// JNI RENDERING //////////////
void fillRenderingAttributes(JNIRenderingContext& rc, RenderingRuleSearchRequest* req) {
	req->clearState();
	req->setIntFilter(req->props()->R_MINZOOM, rc.getZoom());
	if (req->searchRenderingAttribute("defaultColor")) {
		rc.setDefaultColor(req->getIntPropertyValue(req->props()->R_ATTR_COLOR_VALUE));
	}
	if (req->searchRenderingAttribute("waterwayArrows")) {
		rc.setWaterwayArrows(req->getIntPropertyValue(req->props()->R_ATTR_INT_VALUE));
	}
	req->clearState();
	req->setIntFilter(req->props()->R_MINZOOM, rc.getZoom());
	if (req->searchRenderingAttribute("shadowRendering")) {
		rc.setShadowRenderingMode(req->getIntPropertyValue(req->props()->R_ATTR_INT_VALUE));
		rc.setShadowRenderingColor(req->getIntPropertyValue(req->props()->R_SHADOW_COLOR));
	}
	req->clearState();
	req->setIntFilter(req->props()->R_MINZOOM, rc.getZoom());
	if (req->searchRenderingAttribute("polygonMinSizeToDisplay")) {
		rc.polygonMinSizeToDisplay = req->getIntPropertyValue(req->props()->R_ATTR_INT_VALUE);
	}
	req->clearState();
	req->setIntFilter(req->props()->R_MINZOOM, rc.getZoom());
	if (req->searchRenderingAttribute("roadDensityZoomTile")) {
		rc.roadDensityZoomTile = req->getIntPropertyValue(req->props()->R_ATTR_INT_VALUE);
	}
	req->clearState();
	req->setIntFilter(req->props()->R_MINZOOM, rc.getZoom());
	if (req->searchRenderingAttribute("roadsDensityLimitPerTile")) {
		rc.roadsDensityLimitPerTile = req->getIntPropertyValue(req->props()->R_ATTR_INT_VALUE);
	}
}

#ifdef ANDROID_BUILD
#include <android/bitmap.h>

// libJniGraphics interface
typedef int (*PTR_AndroidBitmap_getInfo)(JNIEnv*, jobject, AndroidBitmapInfo*);
typedef int (*PTR_AndroidBitmap_lockPixels)(JNIEnv*, jobject, void**);
typedef int (*PTR_AndroidBitmap_unlockPixels)(JNIEnv*, jobject);
static PTR_AndroidBitmap_getInfo dl_AndroidBitmap_getInfo = 0;
static PTR_AndroidBitmap_lockPixels dl_AndroidBitmap_lockPixels = 0;
static PTR_AndroidBitmap_unlockPixels dl_AndroidBitmap_unlockPixels = 0;
static void* module_libjnigraphics = 0;

void loadModuleJNIGraphics(){	
	if(!module_libjnigraphics)
	{
		module_libjnigraphics = dlopen("jnigraphics", /*RTLD_NOLOAD*/0x0004);
		if(!module_libjnigraphics) {
			OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Warning, "jnigraphics was not found in loaded libraries");
			module_libjnigraphics = dlopen("jnigraphics", RTLD_NOW);
		}
		if(!module_libjnigraphics) {
			OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Warning, "jnigraphics was not loaded in default location");
#if (__x86_64__ || __aarch64__)
			module_libjnigraphics = dlopen("/system/lib64/libjnigraphics.so", RTLD_NOW);
#else
			module_libjnigraphics = dlopen("/system/lib/libjnigraphics.so", RTLD_NOW);
#endif
		}
		if(!module_libjnigraphics)
		{
			OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "Failed to load jnigraphics via dlopen, will going to crash");
			return;
		}
		dl_AndroidBitmap_getInfo = (PTR_AndroidBitmap_getInfo)dlsym(module_libjnigraphics, "AndroidBitmap_getInfo");
		dl_AndroidBitmap_lockPixels = (PTR_AndroidBitmap_lockPixels)dlsym(module_libjnigraphics, "AndroidBitmap_lockPixels");
		dl_AndroidBitmap_unlockPixels = (PTR_AndroidBitmap_unlockPixels)dlsym(module_libjnigraphics, "AndroidBitmap_unlockPixels");
	}
}

extern "C" JNIEXPORT jobject JNICALL Java_net_osmand_plus_render_NativeOsmandLibrary_generateRenderingDirect( JNIEnv* ienv, jobject obj,
    jobject renderingContext, jlong searchResult, jobject targetBitmap, jobject renderingRuleSearchRequest) {

	loadModuleJNIGraphics();

	// Gain information about bitmap
	AndroidBitmapInfo bitmapInfo;
	if(dl_AndroidBitmap_getInfo(ienv, targetBitmap, &bitmapInfo) != ANDROID_BITMAP_RESUT_SUCCESS)
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "Failed to execute AndroidBitmap_getInfo");

	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Creating SkBitmap in native w:%d h:%d s:%d f:%d!", bitmapInfo.width, bitmapInfo.height, bitmapInfo.stride, bitmapInfo.format);

	SkBitmap* bitmap = new SkBitmap();
	SkImageInfo imageInfo;
	if(bitmapInfo.format == ANDROID_BITMAP_FORMAT_RGBA_8888) {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Row bytes for RGBA_8888 is %d", bitmapInfo.stride);
		imageInfo = SkImageInfo::Make(bitmapInfo.width, bitmapInfo.height, kN32_SkColorType, kPremul_SkAlphaType);
	} else if(bitmapInfo.format == ANDROID_BITMAP_FORMAT_RGB_565) {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Row bytes for RGB_565 is %d", bitmapInfo.stride);
		imageInfo = SkImageInfo::Make(bitmapInfo.width, bitmapInfo.height, kRGB_565_SkColorType, kOpaque_SkAlphaType);
	} else {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "Unknown target bitmap format");
	}

	void* lockedBitmapData = NULL;
	if(dl_AndroidBitmap_lockPixels(ienv, targetBitmap, &lockedBitmapData) != ANDROID_BITMAP_RESUT_SUCCESS || !lockedBitmapData) {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "Failed to execute AndroidBitmap_lockPixels");
	}
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Locked %d bytes at %p", bitmapInfo.stride * bitmapInfo.height, lockedBitmapData);

	bitmap->installPixels(imageInfo, lockedBitmapData, bitmapInfo.stride);

	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Initializing rendering");
	OsmAnd::ElapsedTimer initObjects;
	initObjects.Start();

	RenderingRuleSearchRequest* req = initSearchRequest(ienv, renderingRuleSearchRequest);
	JNIRenderingContext rc;
	pullFromJavaRenderingContext(ienv, renderingContext, &rc);
	ResultPublisher* result = ((ResultPublisher*) searchResult);
	fillRenderingAttributes(rc, req);
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Rendering image");
	initObjects.Pause();


	// Main part do rendering
	rc.nativeOperations.Start();
	SkCanvas* canvas = new SkCanvas(*bitmap);
	canvas->drawColor(rc.getDefaultColor());
	if(result != NULL) {
		doRendering(result->result, canvas, req, &rc);
	}

	rc.nativeOperations.Pause();

	pushToJavaRenderingContext(ienv, renderingContext, &rc);
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "End Rendering image");
	if(dl_AndroidBitmap_unlockPixels(ienv, targetBitmap) != ANDROID_BITMAP_RESUT_SUCCESS) {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Error, "Failed to execute AndroidBitmap_unlockPixels");
	}

	// delete  variables
	delete canvas;
	delete req;
	delete bitmap;
	//    deleteObjects(mapDataObjects);

	jclass resultClass = findGlobalClass(ienv, "net/osmand/NativeLibrary$RenderingGenerationResult");

	jmethodID resultClassCtorId = ienv->GetMethodID(resultClass, "<init>", "(Ljava/nio/ByteBuffer;)V");

#ifdef DEBUG_NAT_OPERATIONS
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Native ok (init %d, native op %d) ", (int)initObjects.GetElapsedMs(), (int)rc.nativeOperations.GetElapsedMs());
#else
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Native ok (init %d, rendering %d) ", (int)initObjects.GetElapsedMs(), (int)rc.nativeOperations.GetElapsedMs());
#endif

	/* Construct a result object */
	jobject resultObject = ienv->NewObject(resultClass, resultClassCtorId, NULL);

	return resultObject;
}
#endif


void* bitmapData = NULL;
size_t bitmapDataSize = 0;
extern "C" JNIEXPORT jobject JNICALL Java_net_osmand_NativeLibrary_generateRenderingIndirect( JNIEnv* ienv,
		jobject obj, jobject renderingContext, jlong searchResult, jboolean isTransparent,
		jobject renderingRuleSearchRequest, jboolean encodePNG) {


	JNIRenderingContext rc;
	pullFromJavaRenderingContext(ienv, renderingContext, &rc);

	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Creating SkBitmap in native w:%d h:%d!", rc.getWidth(), rc.getHeight());

	SkBitmap* bitmap = new SkBitmap();
	SkImageInfo imageInfo;
	if (isTransparent == JNI_TRUE)
		imageInfo = SkImageInfo::Make(rc.getWidth(), rc.getHeight(), kN32_SkColorType, kPremul_SkAlphaType);
	else
		imageInfo = SkImageInfo::Make(rc.getWidth(), rc.getHeight(), kRGB_565_SkColorType, kOpaque_SkAlphaType);
	size_t reqDataSize = imageInfo.minRowBytes() * rc.getHeight();

	if (bitmapData != NULL && bitmapDataSize != reqDataSize) {
		free(bitmapData);
		bitmapData = NULL;
		bitmapDataSize = 0;
	}
	if (bitmapData == NULL && bitmapDataSize == 0) {
		bitmapDataSize = reqDataSize;
		bitmapData = malloc(bitmapDataSize);
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Allocated %d bytes at %p", bitmapDataSize, bitmapData);
	}

	bitmap->installPixels(imageInfo, bitmapData, imageInfo.minRowBytes());
	OsmAnd::ElapsedTimer initObjects;
	initObjects.Start();

	RenderingRuleSearchRequest* req = initSearchRequest(ienv, renderingRuleSearchRequest);

	ResultPublisher* result = ((ResultPublisher*) searchResult);
	//    std::vector <BaseMapDataObject* > mapDataObjects = marshalObjects(binaryMapDataObjects);

	initObjects.Pause();
	// Main part do rendering
	fillRenderingAttributes(rc, req);


	SkCanvas* canvas = new SkCanvas(*bitmap);
	canvas->drawColor(rc.getDefaultColor());
	if (result != NULL) {
		doRendering(result->result, canvas, req, &rc);
	}
	pushToJavaRenderingContext(ienv, renderingContext, &rc);

	jclass resultClass = findGlobalClass(ienv, "net/osmand/NativeLibrary$RenderingGenerationResult");

	jmethodID resultClassCtorId = ienv->GetMethodID(resultClass, "<init>", "(Ljava/nio/ByteBuffer;)V");

#ifdef DEBUG_NAT_OPERATIONS
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Native ok (init %d, native op %d) ", (int)initObjects.GetElapsedMs(), (int)rc.nativeOperations.GetElapsedMs());
#else
	OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "Native ok (init %d, rendering %d) ", (int)initObjects.GetElapsedMs(), (int)rc.nativeOperations.GetElapsedMs());
#endif
	// Allocate ctor paramters
	jobject bitmapBuffer;
	if(encodePNG) {
		SkImageEncoder* enc = SkImageEncoder::Create(SkImageEncoder::kPNG_Type);
		SkDynamicMemoryWStream* stream = new SkDynamicMemoryWStream();
		enc->encodeStream(stream, *bitmap, 80);
		// clean previous data
		free(bitmapData);
		bitmapDataSize = stream->bytesWritten();
		bitmapData = malloc(bitmapDataSize);

		stream->copyTo(bitmapData);
		delete enc;
	}
	bitmapBuffer = ienv->NewDirectByteBuffer(bitmapData, bitmapDataSize);

	// delete  variables
	delete canvas;
	delete req;
	delete bitmap;

	fflush(stdout);

	/* Construct a result object */
	jobject resultObject = ienv->NewObject(resultClass, resultClassCtorId, bitmapBuffer);

	return resultObject;
}


///////////////////////////////////////////////
//////////  JNI Rendering Context //////////////

jclass jclass_JUnidecode;
jclass jclass_Reshaper;
jmethodID jmethod_JUnidecode_unidecode;
jmethodID jmethod_Reshaper_reshape;
jclass jclass_RouteCalculationProgress = NULL;
jfieldID jfield_RouteCalculationProgress_segmentNotFound = NULL;
jfieldID jfield_RouteCalculationProgress_distanceFromBegin = NULL;
jfieldID jfield_RouteCalculationProgress_directSegmentQueueSize = NULL;
jfieldID jfield_RouteCalculationProgress_distanceFromEnd = NULL;
jfieldID jfield_RouteCalculationProgress_reverseSegmentQueueSize = NULL;
jfieldID jfield_RouteCalculationProgress_isCancelled = NULL;
jfieldID jfield_RouteCalculationProgress_routingCalculatedTime = NULL;
jfieldID jfield_RouteCalculationProgress_visitedSegments = NULL;
jfieldID jfield_RouteCalculationProgress_loadedTiles = NULL;

jclass jclass_RoutingConfiguration = NULL;
jfieldID jfield_RoutingConfiguration_heuristicCoefficient = NULL;
jfieldID jfield_RoutingConfiguration_ZOOM_TO_LOAD_TILES = NULL;
jfieldID jfield_RoutingConfiguration_planRoadDirection = NULL;
jfieldID jfield_RoutingConfiguration_routerName = NULL;
jfieldID jfield_RoutingConfiguration_router = NULL;

jclass jclass_GeneralRouter = NULL;
jfieldID jfield_GeneralRouter_restrictionsAware = NULL;
jfieldID jfield_GeneralRouter_leftTurn = NULL;
jfieldID jfield_GeneralRouter_roundaboutTurn = NULL;
jfieldID jfield_GeneralRouter_rightTurn = NULL;
jfieldID jfield_GeneralRouter_minDefaultSpeed = NULL;
jfieldID jfield_GeneralRouter_maxDefaultSpeed = NULL;
jfieldID jfield_GeneralRouter_objectAttributes = NULL;
jmethodID jmethod_GeneralRouter_getImpassableRoadIds = NULL;

jclass jclass_RouteAttributeContext = NULL;
jmethodID jmethod_RouteAttributeContext_getRules = NULL;
jmethodID jmethod_RouteAttributeContext_getParamKeys = NULL;
jmethodID jmethod_RouteAttributeContext_getParamValues = NULL;

jclass jclass_RouteAttributeEvalRule = NULL;
jfieldID jfield_RouteAttributeEvalRule_selectValueDef = NULL;
jfieldID jfield_RouteAttributeEvalRule_selectType = NULL;

jmethodID jmethod_RouteAttributeEvalRule_getTagValueCondDefNot = NULL;
jmethodID jmethod_RouteAttributeEvalRule_getTagValueCondDefValue = NULL;
jmethodID jmethod_RouteAttributeEvalRule_getTagValueCondDefTag = NULL;
jmethodID jmethod_RouteAttributeEvalRule_getParameters = NULL;
jmethodID jmethod_RouteAttributeEvalRule_getExpressions = NULL;

jclass jclass_RouteAttributeExpression = NULL;
jfieldID jfield_RouteAttributeExpression_values = NULL;
jfieldID jfield_RouteAttributeExpression_expressionType= NULL;
jfieldID jfield_RouteAttributeExpression_valueType = NULL;

jclass jclass_PrecalculatedRouteDirection = NULL;
jfieldID jfield_PrecalculatedRouteDirection_tms = NULL;
jfieldID jfield_PrecalculatedRouteDirection_pointsY = NULL;
jfieldID jfield_PrecalculatedRouteDirection_pointsX = NULL;
jfieldID jfield_PrecalculatedRouteDirection_minSpeed = NULL;
jfieldID jfield_PrecalculatedRouteDirection_maxSpeed = NULL;
jfieldID jfield_PrecalculatedRouteDirection_followNext= NULL;
jfieldID jfield_PrecalculatedRouteDirection_endFinishTime = NULL;
jfieldID jfield_PrecalculatedRouteDirection_startFinishTime = NULL;

jclass jclass_RenderingContext = NULL;
jfieldID jfield_RenderingContext_interrupted = NULL;
jfieldID jfield_RenderingContext_leftX = NULL;
jfieldID jfield_RenderingContext_topY = NULL;
jfieldID jfield_RenderingContext_width = NULL;
jfieldID jfield_RenderingContext_height = NULL;
jfieldID jfield_RenderingContext_zoom = NULL;
jfieldID jfield_RenderingContext_tileDivisor = NULL;
jfieldID jfield_RenderingContext_rotate = NULL;
jfieldID jfield_RenderingContext_preferredLocale = NULL;
jfieldID jfield_RenderingContext_pointCount = NULL;
jfieldID jfield_RenderingContext_pointInsideCount = NULL;
jfieldID jfield_RenderingContext_visible = NULL;
jfieldID jfield_RenderingContext_allObjects = NULL;
jfieldID jfield_RenderingContext_density = NULL;
jfieldID jfield_RenderingContext_textScale = NULL;
jfieldID jfield_RenderingContext_screenDensityRatio = NULL;
jfieldID jfield_RenderingContext_shadowRenderingMode = NULL;
jfieldID jfield_RenderingContext_shadowRenderingColor = NULL;
jfieldID jfield_RenderingContext_defaultColor = NULL;
jfieldID jfield_RenderingContext_textRenderingTime = NULL;
jfieldID jfield_RenderingContext_lastRenderedKey = NULL;

jmethodID jmethod_RenderingContext_getIconRawData = NULL;

jclass jclass_RouteDataObject = NULL;
jfieldID jfield_RouteDataObject_types = NULL;
jfieldID jfield_RouteDataObject_pointsX = NULL;
jfieldID jfield_RouteDataObject_pointsY = NULL;
jfieldID jfield_RouteDataObject_restrictions = NULL;
jfieldID jfield_RouteDataObject_pointTypes = NULL;
jfieldID jfield_RouteDataObject_pointNames = NULL;
jfieldID jfield_RouteDataObject_pointNameTypes = NULL;
jfieldID jfield_RouteDataObject_id = NULL;
jmethodID jmethod_RouteDataObject_init = NULL;

jclass jclass_NativeRouteSearchResult = NULL;
jmethodID jmethod_NativeRouteSearchResult_init = NULL;


jclass jclass_RouteSubregion = NULL;
jfieldID jfield_RouteSubregion_length = NULL;
jfieldID jfield_RouteSubregion_filePointer= NULL;
jfieldID jfield_RouteSubregion_left = NULL;
jfieldID jfield_RouteSubregion_right = NULL;
jfieldID jfield_RouteSubregion_top = NULL;
jfieldID jfield_RouteSubregion_bottom = NULL;
jfieldID jfield_RouteSubregion_shiftToData = NULL;

jclass jclass_RouteRegion = NULL;
jfieldID jfield_RouteRegion_length = NULL;
jfieldID jfield_RouteRegion_filePointer= NULL;

jclass jclass_RouteSegmentResult = NULL;
jclass jclass_RouteSegmentResultAr = NULL;
jmethodID jmethod_RouteSegmentResult_ctor = NULL;
jfieldID jfield_RouteSegmentResult_preAttachedRoutes = NULL;
jfieldID jfield_RouteSegmentResult_routingTime = NULL;



void loadJniRenderingContext(JNIEnv* env)
{
	jclass_RouteSegmentResult = findGlobalClass(env, "net/osmand/router/RouteSegmentResult");
	jclass_RouteSegmentResultAr = findGlobalClass(env, "[Lnet/osmand/router/RouteSegmentResult;");
	jmethod_RouteSegmentResult_ctor = env->GetMethodID(jclass_RouteSegmentResult,
			"<init>", "(Lnet/osmand/binary/RouteDataObject;II)V");
	jfield_RouteSegmentResult_routingTime = getFid(env, jclass_RouteSegmentResult, "routingTime",
			"F"); 
	jfield_RouteSegmentResult_preAttachedRoutes = getFid(env, jclass_RouteSegmentResult, "preAttachedRoutes",
			"[[Lnet/osmand/router/RouteSegmentResult;");

	jclass_RouteCalculationProgress = findGlobalClass(env, "net/osmand/router/RouteCalculationProgress");
	jfield_RouteCalculationProgress_isCancelled  = getFid(env, jclass_RouteCalculationProgress, "isCancelled", "Z");
	jfield_RouteCalculationProgress_segmentNotFound  = getFid(env, jclass_RouteCalculationProgress, "segmentNotFound", "I");
	jfield_RouteCalculationProgress_distanceFromBegin  = getFid(env, jclass_RouteCalculationProgress, "distanceFromBegin", "F");
	jfield_RouteCalculationProgress_distanceFromEnd  = getFid(env, jclass_RouteCalculationProgress, "distanceFromEnd", "F");
	jfield_RouteCalculationProgress_directSegmentQueueSize  = getFid(env, jclass_RouteCalculationProgress, "directSegmentQueueSize", "I");
	jfield_RouteCalculationProgress_reverseSegmentQueueSize  = getFid(env, jclass_RouteCalculationProgress, "reverseSegmentQueueSize", "I");
	jfield_RouteCalculationProgress_routingCalculatedTime  = getFid(env, jclass_RouteCalculationProgress, "routingCalculatedTime", "F");
	jfield_RouteCalculationProgress_visitedSegments  = getFid(env, jclass_RouteCalculationProgress, "visitedSegments", "I");
	jfield_RouteCalculationProgress_loadedTiles  = getFid(env, jclass_RouteCalculationProgress, "loadedTiles", "I");

	jclass_RoutingConfiguration = findGlobalClass(env, "net/osmand/router/RoutingConfiguration");
	jfield_RoutingConfiguration_heuristicCoefficient = getFid(env, jclass_RoutingConfiguration, "heuristicCoefficient", "F");
	jfield_RoutingConfiguration_ZOOM_TO_LOAD_TILES = getFid(env, jclass_RoutingConfiguration, "ZOOM_TO_LOAD_TILES", "I");
	jfield_RoutingConfiguration_planRoadDirection = getFid(env, jclass_RoutingConfiguration, "planRoadDirection", "I");
	jfield_RoutingConfiguration_routerName = getFid(env, jclass_RoutingConfiguration, "routerName", "Ljava/lang/String;");
	jfield_RoutingConfiguration_router = getFid(env, jclass_RoutingConfiguration, "router", "Lnet/osmand/router/GeneralRouter;");

	jclass_GeneralRouter = findGlobalClass(env, "net/osmand/router/GeneralRouter");
	jfield_GeneralRouter_restrictionsAware = getFid(env, jclass_GeneralRouter, "restrictionsAware", "Z");
	jfield_GeneralRouter_leftTurn = getFid(env, jclass_GeneralRouter, "leftTurn", "F");
	jfield_GeneralRouter_roundaboutTurn = getFid(env, jclass_GeneralRouter, "roundaboutTurn", "F");
	jfield_GeneralRouter_rightTurn = getFid(env, jclass_GeneralRouter, "rightTurn", "F");
	jfield_GeneralRouter_minDefaultSpeed = getFid(env, jclass_GeneralRouter, "minDefaultSpeed", "F");
	jfield_GeneralRouter_maxDefaultSpeed = getFid(env, jclass_GeneralRouter, "maxDefaultSpeed", "F");
	jfield_GeneralRouter_objectAttributes = getFid(env, jclass_GeneralRouter, "objectAttributes", 
		"[Lnet/osmand/router/GeneralRouter$RouteAttributeContext;");
	jmethod_GeneralRouter_getImpassableRoadIds = env->GetMethodID(jclass_GeneralRouter,
				"getImpassableRoadIds", "()[J");

	jclass_RouteAttributeContext = findGlobalClass(env, "net/osmand/router/GeneralRouter$RouteAttributeContext");	
	jmethod_RouteAttributeContext_getRules = env->GetMethodID(jclass_RouteAttributeContext,
				"getRules", "()[Lnet/osmand/router/GeneralRouter$RouteAttributeEvalRule;");
	jmethod_RouteAttributeContext_getParamKeys = env->GetMethodID(jclass_RouteAttributeContext,
				"getParamKeys", "()[Ljava/lang/String;");
	jmethod_RouteAttributeContext_getParamValues = env->GetMethodID(jclass_RouteAttributeContext,
				"getParamValues", "()[Ljava/lang/String;");

	jclass_RouteAttributeEvalRule = findGlobalClass(env, "net/osmand/router/GeneralRouter$RouteAttributeEvalRule");
	jfield_RouteAttributeEvalRule_selectValueDef = getFid(env, jclass_RouteAttributeEvalRule, "selectValueDef", "Ljava/lang/String;");
	jfield_RouteAttributeEvalRule_selectType = getFid(env, jclass_RouteAttributeEvalRule, "selectType", "Ljava/lang/String;");

	jmethod_RouteAttributeEvalRule_getTagValueCondDefNot = env->GetMethodID(jclass_RouteAttributeEvalRule,
				"getTagValueCondDefNot", "()[Z");
	jmethod_RouteAttributeEvalRule_getTagValueCondDefValue = env->GetMethodID(jclass_RouteAttributeEvalRule,
				"getTagValueCondDefValue", "()[Ljava/lang/String;");
	jmethod_RouteAttributeEvalRule_getTagValueCondDefTag = env->GetMethodID(jclass_RouteAttributeEvalRule,
				"getTagValueCondDefTag", "()[Ljava/lang/String;");
	jmethod_RouteAttributeEvalRule_getParameters = env->GetMethodID(jclass_RouteAttributeEvalRule,
				"getParameters", "()[Ljava/lang/String;");					
	jmethod_RouteAttributeEvalRule_getExpressions = env->GetMethodID(jclass_RouteAttributeEvalRule,
				"getExpressions", "()[Lnet/osmand/router/GeneralRouter$RouteAttributeExpression;");

	jclass_RouteAttributeExpression = findGlobalClass(env, "net/osmand/router/GeneralRouter$RouteAttributeExpression");	
    jfield_RouteAttributeExpression_values = getFid(env, jclass_RouteAttributeExpression, "values", "[Ljava/lang/String;");
   	jfield_RouteAttributeExpression_expressionType = getFid(env, jclass_RouteAttributeExpression, "expressionType", "I");
   	jfield_RouteAttributeExpression_valueType = getFid(env, jclass_RouteAttributeExpression, "valueType", "Ljava/lang/String;");
	

	jclass_PrecalculatedRouteDirection = findGlobalClass(env, "net/osmand/router/PrecalculatedRouteDirection");
	jfield_PrecalculatedRouteDirection_tms = getFid(env, jclass_PrecalculatedRouteDirection, "tms", "[F");
	jfield_PrecalculatedRouteDirection_pointsY = getFid(env, jclass_PrecalculatedRouteDirection, "pointsY", "[I");
	jfield_PrecalculatedRouteDirection_pointsX = getFid(env, jclass_PrecalculatedRouteDirection, "pointsX", "[I");
	jfield_PrecalculatedRouteDirection_minSpeed = getFid(env, jclass_PrecalculatedRouteDirection, "minSpeed", "F");
	jfield_PrecalculatedRouteDirection_maxSpeed = getFid(env, jclass_PrecalculatedRouteDirection, "maxSpeed", "F");
	jfield_PrecalculatedRouteDirection_followNext = getFid(env, jclass_PrecalculatedRouteDirection, "followNext", "Z");
	jfield_PrecalculatedRouteDirection_endFinishTime = getFid(env, jclass_PrecalculatedRouteDirection, "endFinishTime", "F");
	jfield_PrecalculatedRouteDirection_startFinishTime = getFid(env, jclass_PrecalculatedRouteDirection, "startFinishTime", "F");

	jclass_RenderingContext = findGlobalClass(env, "net/osmand/RenderingContext");
	jfield_RenderingContext_interrupted = getFid(env, jclass_RenderingContext, "interrupted", "Z");
	jfield_RenderingContext_leftX = getFid(env,  jclass_RenderingContext, "leftX", "D" );
	jfield_RenderingContext_topY = getFid(env,  jclass_RenderingContext, "topY", "D" );
	jfield_RenderingContext_width = getFid(env,  jclass_RenderingContext, "width", "I" );
	jfield_RenderingContext_height = getFid(env,  jclass_RenderingContext, "height", "I" );
	jfield_RenderingContext_zoom = getFid(env,  jclass_RenderingContext, "zoom", "I" );
	jfield_RenderingContext_tileDivisor = getFid(env,  jclass_RenderingContext, "tileDivisor", "D" );
	jfield_RenderingContext_rotate = getFid(env,  jclass_RenderingContext, "rotate", "F" );
	jfield_RenderingContext_preferredLocale = getFid(env,  jclass_RenderingContext, "preferredLocale", "Ljava/lang/String;" );
	jfield_RenderingContext_pointCount = getFid(env,  jclass_RenderingContext, "pointCount", "I" );
	jfield_RenderingContext_pointInsideCount = getFid(env,  jclass_RenderingContext, "pointInsideCount", "I" );
	jfield_RenderingContext_visible = getFid(env,  jclass_RenderingContext, "visible", "I" );
	jfield_RenderingContext_allObjects = getFid(env,  jclass_RenderingContext, "allObjects", "I" );
	jfield_RenderingContext_density = getFid(env,  jclass_RenderingContext, "density", "F" );
	jfield_RenderingContext_textScale = getFid(env,  jclass_RenderingContext, "textScale", "F" );
	jfield_RenderingContext_screenDensityRatio = getFid(env,  jclass_RenderingContext, "screenDensityRatio", "F" );	
	jfield_RenderingContext_shadowRenderingMode = getFid(env,  jclass_RenderingContext, "shadowRenderingMode", "I" );
	jfield_RenderingContext_shadowRenderingColor = getFid(env,  jclass_RenderingContext, "shadowRenderingColor", "I" );
	jfield_RenderingContext_defaultColor = getFid(env,  jclass_RenderingContext, "defaultColor", "I" );
	jfield_RenderingContext_textRenderingTime = getFid(env,  jclass_RenderingContext, "textRenderingTime", "I" );
	jfield_RenderingContext_lastRenderedKey = getFid(env,  jclass_RenderingContext, "lastRenderedKey", "I" );
	jmethod_RenderingContext_getIconRawData = env->GetMethodID(jclass_RenderingContext,
				"getIconRawData", "(Ljava/lang/String;)[B");

	jmethod_Object_toString = env->GetMethodID(findGlobalClass(env, "java/lang/Object"),
					"toString", "()Ljava/lang/String;");


	jclass_JUnidecode = findGlobalClass(env, "net/sf/junidecode/Junidecode");
    jmethod_JUnidecode_unidecode = env->GetStaticMethodID(jclass_JUnidecode, "unidecode", "(Ljava/lang/String;)Ljava/lang/String;");
    jclass_Reshaper = findGlobalClass(env, "net/osmand/Reshaper");
    jmethod_Reshaper_reshape = env->GetStaticMethodID(jclass_Reshaper, "reshape", "(Ljava/lang/String;)Ljava/lang/String;");

    jclass_RouteDataObject = findGlobalClass(env, "net/osmand/binary/RouteDataObject");
    jclass_NativeRouteSearchResult = findGlobalClass(env, "net/osmand/NativeLibrary$NativeRouteSearchResult");
    jmethod_NativeRouteSearchResult_init = env->GetMethodID(jclass_NativeRouteSearchResult, "<init>", "(J[Lnet/osmand/binary/RouteDataObject;)V");

    jfield_RouteDataObject_types = getFid(env,  jclass_RouteDataObject, "types", "[I" );
    jfield_RouteDataObject_pointsX = getFid(env,  jclass_RouteDataObject, "pointsX", "[I" );
    jfield_RouteDataObject_pointsY = getFid(env,  jclass_RouteDataObject, "pointsY", "[I" );
    jfield_RouteDataObject_restrictions = getFid(env,  jclass_RouteDataObject, "restrictions", "[J" );
    jfield_RouteDataObject_pointTypes = getFid(env,  jclass_RouteDataObject, "pointTypes", "[[I" );
    jfield_RouteDataObject_pointNameTypes = getFid(env,  jclass_RouteDataObject, "pointNameTypes", "[[I" );
    jfield_RouteDataObject_pointNames = getFid(env,  jclass_RouteDataObject, "pointNames", "[[Ljava/lang/String;" );
    jfield_RouteDataObject_id = getFid(env,  jclass_RouteDataObject, "id", "J" );
    jmethod_RouteDataObject_init = env->GetMethodID(jclass_RouteDataObject, "<init>", "(Lnet/osmand/binary/BinaryMapRouteReaderAdapter$RouteRegion;[I[Ljava/lang/String;)V");


    jclass_RouteRegion = findGlobalClass(env, "net/osmand/binary/BinaryMapRouteReaderAdapter$RouteRegion");
    jfield_RouteRegion_length= getFid(env,  jclass_RouteRegion, "length", "I" );
    jfield_RouteRegion_filePointer= getFid(env,  jclass_RouteRegion, "filePointer", "I" );

    jclass_RouteSubregion = findGlobalClass(env, "net/osmand/binary/BinaryMapRouteReaderAdapter$RouteSubregion");
    jfield_RouteSubregion_length= getFid(env,  jclass_RouteSubregion, "length", "I" );
    jfield_RouteSubregion_filePointer= getFid(env,  jclass_RouteSubregion, "filePointer", "I" );
    jfield_RouteSubregion_left= getFid(env,  jclass_RouteSubregion, "left", "I" );
    jfield_RouteSubregion_right= getFid(env,  jclass_RouteSubregion, "right", "I" );
    jfield_RouteSubregion_top= getFid(env,  jclass_RouteSubregion, "top", "I" );
    jfield_RouteSubregion_bottom= getFid(env,  jclass_RouteSubregion, "bottom", "I" );
    jfield_RouteSubregion_shiftToData= getFid(env,  jclass_RouteSubregion, "shiftToData", "I" );
	// public final RouteRegion routeReg;

}

void pullFromJavaRenderingContext(JNIEnv* env, jobject jrc, JNIRenderingContext* rc)
{
	rc->env = env;
	rc->setLocation(env->GetDoubleField( jrc, jfield_RenderingContext_leftX ), env->GetDoubleField( jrc, jfield_RenderingContext_topY ));
	rc->setDimension(env->GetIntField( jrc, jfield_RenderingContext_width ), env->GetIntField( jrc, jfield_RenderingContext_height ));

	rc->setZoom(env->GetIntField( jrc, jfield_RenderingContext_zoom ));
	rc->setTileDivisor(env->GetDoubleField( jrc, jfield_RenderingContext_tileDivisor ));
	rc->setRotate(env->GetFloatField( jrc, jfield_RenderingContext_rotate ));
	rc->setDensityScale(env->GetFloatField( jrc, jfield_RenderingContext_density ));
	rc->setTextScale(env->GetFloatField( jrc, jfield_RenderingContext_textScale ));
	rc->setScreenDensityRatio(env->GetFloatField( jrc, jfield_RenderingContext_screenDensityRatio ));
	
	jstring jpref = (jstring) env->GetObjectField(jrc, jfield_RenderingContext_preferredLocale);
	rc->setPreferredLocale(getString(env, jpref));
	env->DeleteLocalRef(jpref);
	rc->javaRenderingContext = jrc;
}


// ElapsedTimer routingTimer;

jobject convertRouteDataObjectToJava(JNIEnv* ienv, RouteDataObject* route, jobject reg) {
	jintArray nameInts = ienv->NewIntArray(route->names.size());
	jobjectArray nameStrings = ienv->NewObjectArray(route->names.size(), jclassString, NULL);
	jint* ar = new jint[route->names.size()];//NEVER DEALLOCATED
	UNORDERED(map)<int, std::string >::iterator itNames = route->names.begin();
	jsize sz = 0;
	for (; itNames != route->names.end(); itNames++, sz++) {
		std::string name = itNames->second;
		jstring js = ienv->NewStringUTF(name.c_str());
		ienv->SetObjectArrayElement(nameStrings, sz, js);
		ienv->DeleteLocalRef(js);
		ar[sz] = itNames->first;
	}
	ienv->SetIntArrayRegion(nameInts, 0, route->names.size(), ar);
	jobject robj = ienv->NewObject(jclass_RouteDataObject, jmethod_RouteDataObject_init, reg, nameInts, nameStrings);
	ienv->DeleteLocalRef(nameInts);
	ienv->DeleteLocalRef(nameStrings);

	ienv->SetLongField(robj, jfield_RouteDataObject_id, route->id);

	jintArray types = ienv->NewIntArray(route->types.size());
	if (route->types.size() > 0) {
		ienv->SetIntArrayRegion(types, 0, route->types.size(), (jint*) &route->types[0]);
	}
	ienv->SetObjectField(robj, jfield_RouteDataObject_types, types);
	ienv->DeleteLocalRef(types);

	jintArray pointsX = ienv->NewIntArray(route->pointsX.size());
	if (route->pointsX.size() > 0) {
		ienv->SetIntArrayRegion(pointsX, 0, route->pointsX.size(), (jint*) &route->pointsX[0]);
	}
	ienv->SetObjectField(robj, jfield_RouteDataObject_pointsX, pointsX);
	ienv->DeleteLocalRef(pointsX);

	jintArray pointsY = ienv->NewIntArray(route->pointsY.size());
	if (route->pointsY.size() > 0) {
		ienv->SetIntArrayRegion(pointsY, 0, route->pointsY.size(), (jint*) &route->pointsY[0]);
	}
	ienv->SetObjectField(robj, jfield_RouteDataObject_pointsY, pointsY);
	ienv->DeleteLocalRef(pointsY);

	jlongArray restrictions = ienv->NewLongArray(route->restrictions.size());
	if (route->restrictions.size() > 0) {
		ienv->SetLongArrayRegion(restrictions, 0, route->restrictions.size(), (jlong*) &route->restrictions[0]);
	}
	ienv->SetObjectField(robj, jfield_RouteDataObject_restrictions, restrictions);
	ienv->DeleteLocalRef(restrictions);

	jobjectArray pointTypes = ienv->NewObjectArray(route->pointTypes.size(), jclassIntArray, NULL);
	for (uint k = 0; k < route->pointTypes.size(); k++) {
		std::vector<uint32_t> ts = route->pointTypes[k];
		if (ts.size() > 0) {
			jintArray tos = ienv->NewIntArray(ts.size());
			ienv->SetIntArrayRegion(tos, 0, ts.size(), (jint*) &ts[0]);
			ienv->SetObjectArrayElement(pointTypes, k, tos);
			ienv->DeleteLocalRef(tos);
		}
	}

	ienv->SetObjectField(robj, jfield_RouteDataObject_pointTypes, pointTypes);
	ienv->DeleteLocalRef(pointTypes);

	if(route->pointNameTypes.size() > 0) {
		jobjectArray pointNameTypes = ienv->NewObjectArray(route->pointNameTypes.size(), jclassIntArray, NULL);
		for (uint k = 0; k < route->pointNameTypes.size(); k++) {
			std::vector<uint32_t> ts = route->pointNameTypes[k];
			if (ts.size() > 0) {
				jintArray tos = ienv->NewIntArray(ts.size());
				ienv->SetIntArrayRegion(tos, 0, ts.size(), (jint*) &ts[0]);
				ienv->SetObjectArrayElement(pointNameTypes, k, tos);
				ienv->DeleteLocalRef(tos);
			}
		}
	
		ienv->SetObjectField(robj, jfield_RouteDataObject_pointNameTypes, pointNameTypes);
		ienv->DeleteLocalRef(pointNameTypes); 
	}

	if(route->pointNames.size() > 0) {
		jobjectArray pointNames = ienv->NewObjectArray(route->pointNames.size(), jclassStringArray, NULL);
		for (uint k = 0; k < route->pointNames.size(); k++) {
			std::vector<std::string> ts = route->pointNames[k];
			if(ts.size() > 0 ) {
				jobjectArray nameStrings = ienv->NewObjectArray(ts.size(), jclassString, NULL);
				jsize sz = 0;
				for (uint p = 0; p < ts.size(); p++, sz++) {
					jstring js = ienv->NewStringUTF(ts[p].c_str());
					ienv->SetObjectArrayElement(nameStrings, sz, js);
					ienv->DeleteLocalRef(js);
				}
				ienv->SetObjectArrayElement(pointNames, k, nameStrings);
				ienv->DeleteLocalRef(nameStrings);
			}	
		}
		ienv->SetObjectField(robj, jfield_RouteDataObject_pointNames, pointNames);
		ienv->DeleteLocalRef(pointNames);
	}

	return robj;
}

jobject convertRouteSegmentResultToJava(JNIEnv* ienv, RouteSegmentResult& r, UNORDERED(map)<int64_t, int>& indexes,
		jobjectArray regions) {
	RouteDataObject* rdo = r.object.get();
	jobject reg = NULL;
	int64_t fp = rdo->region->filePointer;
	int64_t ln = rdo->region->length;
	if(indexes.find((fp <<31) + ln) != indexes.end()) {
		reg = ienv->GetObjectArrayElement(regions, indexes[(fp <<31) + ln]);
	}
	jobjectArray ar = ienv->NewObjectArray(r.attachedRoutes.size(), jclass_RouteSegmentResultAr, NULL);
	for(jsize k = 0; k < (jsize)r.attachedRoutes.size(); k++) {
		jobjectArray art = ienv->NewObjectArray(r.attachedRoutes[k].size(), jclass_RouteSegmentResult, NULL);
		for(jsize kj = 0; kj < (jsize)r.attachedRoutes[k].size(); kj++) {
			jobject jo = convertRouteSegmentResultToJava(ienv, r.attachedRoutes[k][kj], indexes, regions);
			ienv->SetObjectArrayElement(art, kj, jo);
			ienv->DeleteLocalRef(jo);
		}
		ienv->SetObjectArrayElement(ar, k, art);
		ienv->DeleteLocalRef(art);
	}
	jobject robj = convertRouteDataObjectToJava(ienv, rdo, reg);
	jobject resobj = ienv->NewObject(jclass_RouteSegmentResult, jmethod_RouteSegmentResult_ctor, robj,
			r.startPointIndex, r.endPointIndex);
	ienv->SetFloatField(resobj, jfield_RouteSegmentResult_routingTime, (jfloat)r.routingTime);
	ienv->SetObjectField(resobj, jfield_RouteSegmentResult_preAttachedRoutes, ar);
	if(reg != NULL) {
		ienv->DeleteLocalRef(reg);
	}
	ienv->DeleteLocalRef(robj);
	ienv->DeleteLocalRef(ar);
	return resobj;
}

class NativeRoutingTile {
public:
	std::vector<RouteDataObject*> result;
	UNORDERED(map)<uint64_t, std::vector<RouteDataObject*> > cachedByLocations;
};


//	protected static native void deleteRouteSearchResult(long searchResultHandle!);
extern "C" JNIEXPORT void JNICALL Java_net_osmand_NativeLibrary_deleteRouteSearchResult(JNIEnv* ienv,
		jobject obj, jlong ref) {
	NativeRoutingTile* t = (NativeRoutingTile*) ref;
	for (unsigned int i = 0; i < t->result.size(); i++) {
		delete t->result[i];
		t->result[i] = NULL;
	}
	delete t;
}

class RouteCalculationProgressWrapper: public RouteCalculationProgress {
	JNIEnv* ienv;
	jobject j;
public:
	RouteCalculationProgressWrapper(JNIEnv* ienv, jobject j) : RouteCalculationProgress(),
			ienv(ienv), j(j)  {
	}
	virtual bool isCancelled() {
		if(j == NULL) {
			return false;
		}
		return ienv->GetBooleanField(j, jfield_RouteCalculationProgress_isCancelled);
	}
	virtual void setSegmentNotFound(int s) {
		if(j != NULL) {
			ienv->SetIntField(j, jfield_RouteCalculationProgress_segmentNotFound, s);
		}
	}
	virtual void updateStatus(float distanceFromBegin, int directSegmentQueueSize, float distanceFromEnd,
			int reverseSegmentQueueSize) {
		RouteCalculationProgress::updateStatus(distanceFromBegin, directSegmentQueueSize,
				distanceFromEnd, reverseSegmentQueueSize);
		if(j != NULL) {
		   ienv->SetFloatField(j, jfield_RouteCalculationProgress_distanceFromBegin, this->distanceFromBegin);
		   ienv->SetFloatField(j, jfield_RouteCalculationProgress_distanceFromEnd, this->distanceFromEnd);
		   ienv->SetIntField(j, jfield_RouteCalculationProgress_directSegmentQueueSize, this->directSegmentQueueSize);
		   ienv->SetIntField(j, jfield_RouteCalculationProgress_reverseSegmentQueueSize, this->reverseSegmentQueueSize);
        }
	}
};

void parsePrecalculatedRoute(JNIEnv* ienv, RoutingContext& ctx,  jobject precalculatedRoute) {
	if(precalculatedRoute != NULL) {
		ctx.precalcRoute.empty = false;
		jintArray pointsY = (jintArray) ienv->GetObjectField(precalculatedRoute, jfield_PrecalculatedRouteDirection_pointsY);
		jintArray pointsX = (jintArray) ienv->GetObjectField(precalculatedRoute, jfield_PrecalculatedRouteDirection_pointsX);
		jfloatArray tms = (jfloatArray) ienv->GetObjectField(precalculatedRoute, jfield_PrecalculatedRouteDirection_tms);
		jint* pointsYF = (jint*)ienv->GetIntArrayElements(pointsY, NULL);
		jint* pointsXF = (jint*)ienv->GetIntArrayElements(pointsX, NULL);
		jfloat* tmsF = (jfloat*)ienv->GetFloatArrayElements(tms, NULL);
		for(int k = 0; k < ienv->GetArrayLength(pointsY); k++) {
			int y = pointsYF[k];
			int x = pointsXF[k];
			int ind = ctx.precalcRoute.pointsX.size();
			ctx.precalcRoute.pointsY.push_back(y);
			ctx.precalcRoute.pointsX.push_back(x);
			ctx.precalcRoute.times.push_back(tmsF[k]);
			SkRect r = SkRect::MakeLTRB(x, y, x, y);
			ctx.precalcRoute.quadTree.insert(ind, r);
		}
		ctx.precalcRoute.startPoint = ctx.precalcRoute.calc(ctx.startX, ctx.startY);
		ctx.precalcRoute.endPoint = ctx.precalcRoute.calc(ctx.targetX, ctx.targetY);
		ctx.precalcRoute.minSpeed = ienv->GetFloatField(precalculatedRoute, jfield_PrecalculatedRouteDirection_minSpeed);
		ctx.precalcRoute.maxSpeed = ienv->GetFloatField(precalculatedRoute, jfield_PrecalculatedRouteDirection_maxSpeed);
		ctx.precalcRoute.followNext = ienv->GetBooleanField(precalculatedRoute, jfield_PrecalculatedRouteDirection_followNext);
		ctx.precalcRoute.startFinishTime = ienv->GetFloatField(precalculatedRoute, jfield_PrecalculatedRouteDirection_startFinishTime);
		ctx.precalcRoute.endFinishTime = ienv->GetFloatField(precalculatedRoute, jfield_PrecalculatedRouteDirection_endFinishTime);
		ienv->ReleaseIntArrayElements(pointsY, pointsYF, 0);
		ienv->ReleaseIntArrayElements(pointsX, pointsXF, 0);
		ienv->ReleaseFloatArrayElements(tms, tmsF, 0);

	}
}

vector<string> convertJArrayToStrings(JNIEnv* ienv, jobjectArray ar) {
	vector<string> res;
	for(int i = 0; i < ienv->GetArrayLength(ar); i++) {
		// RouteAttributeContext
		jstring s = (jstring) ienv->GetObjectArrayElement(ar, i);
		if(s == NULL) {
			res.push_back("");
		} else {
			res.push_back(getString(ienv, s));
			ienv->DeleteLocalRef(s);
		}
	}
	return res;
}

void parseRouteAttributeEvalRule(JNIEnv* ienv, jobject rule, RouteAttributeEvalRule* erule, GeneralRouter* router) {
	jstring jselectValue = (jstring) ienv->GetObjectField(rule, jfield_RouteAttributeEvalRule_selectValueDef);
	string selectValue = getString(ienv, jselectValue);
	ienv->DeleteLocalRef(jselectValue);
	jstring jselectType = (jstring) ienv->GetObjectField(rule, jfield_RouteAttributeEvalRule_selectType);
	string selectType;
	if(jselectType) {
		selectType = getString(ienv, jselectType);
		ienv->DeleteLocalRef(jselectType);
	}

	erule->registerSelectValue(selectValue, selectType); 

	jobjectArray ar = (jobjectArray) ienv->CallObjectMethod(rule, jmethod_RouteAttributeEvalRule_getParameters);
	vector<string> params = convertJArrayToStrings(ienv, ar);
	ienv->DeleteLocalRef(ar);
	erule->registerParamConditions(params); 

	ar = (jobjectArray) ienv->CallObjectMethod(rule, jmethod_RouteAttributeEvalRule_getTagValueCondDefValue);
	vector<string> tagValueDefValues = convertJArrayToStrings(ienv, ar);
	ienv->DeleteLocalRef(ar);

	ar = (jobjectArray) ienv->CallObjectMethod(rule, jmethod_RouteAttributeEvalRule_getTagValueCondDefTag);
	vector<string> tagValueDefTags = convertJArrayToStrings(ienv, ar);
	ienv->DeleteLocalRef(ar);

	jbooleanArray tagValueDefNot = (jbooleanArray)ienv->CallObjectMethod(rule, jmethod_RouteAttributeEvalRule_getTagValueCondDefNot);
	jboolean* nots = ienv->GetBooleanArrayElements(tagValueDefNot, NULL);
	for(uint i = 0; i < tagValueDefValues.size(); i++) {
		erule->registerAndTagValueCondition(router, tagValueDefTags[i], tagValueDefValues[i], nots[i]);
	}
	ienv->ReleaseBooleanArrayElements(tagValueDefNot, nots, 0);
	ienv->DeleteLocalRef(tagValueDefNot);
	

	jobjectArray expressions = (jobjectArray) ienv->CallObjectMethod(rule, jmethod_RouteAttributeEvalRule_getExpressions);
	for(int j = 0; j < ienv->GetArrayLength(expressions); j++) {
		jobject expr = ienv->GetObjectArrayElement(expressions, j);
		jobjectArray jvls  = (jobjectArray) ienv->GetObjectField(expr, jfield_RouteAttributeExpression_values);
		vector<string> values = convertJArrayToStrings(ienv, jvls);
		ienv->DeleteLocalRef(jvls);
		int expressionType = ienv->GetIntField(expr, jfield_RouteAttributeExpression_expressionType);
		
		jstring jvalueType = (jstring) ienv->GetObjectField(expr, jfield_RouteAttributeExpression_valueType);
		string valueType;
		if(jselectType) {
			valueType = getString(ienv, jvalueType);
			ienv->DeleteLocalRef(jvalueType);
		}

		RouteAttributeExpression e(values, expressionType, valueType);
		erule->registerExpression(e);
		ienv->DeleteLocalRef(expr);
	}
	ienv->DeleteLocalRef(expressions);

}

void parseRouteConfiguration(JNIEnv* ienv, RoutingConfiguration& rConfig, jobject jRouteConfig) {
	rConfig.planRoadDirection = ienv->GetIntField(jRouteConfig, jfield_RoutingConfiguration_planRoadDirection);
	rConfig.heurCoefficient = ienv->GetFloatField(jRouteConfig, jfield_RoutingConfiguration_heuristicCoefficient);
	rConfig.zoomToLoad = ienv->GetIntField(jRouteConfig, jfield_RoutingConfiguration_ZOOM_TO_LOAD_TILES);
	jstring rName = (jstring) ienv->GetObjectField(jRouteConfig, jfield_RoutingConfiguration_routerName);
	rConfig.routerName = getString(ienv, rName);

	jobject lrouter = ienv->GetObjectField(jRouteConfig, jfield_RoutingConfiguration_router);
	jobject router = ienv->NewGlobalRef(lrouter);
	rConfig.router._restrictionsAware = ienv->GetBooleanField(router, jfield_GeneralRouter_restrictionsAware);
	rConfig.router.leftTurn = ienv->GetFloatField(router, jfield_GeneralRouter_leftTurn);
	rConfig.router.roundaboutTurn = ienv->GetFloatField(router, jfield_GeneralRouter_roundaboutTurn);
	rConfig.router.rightTurn = ienv->GetFloatField(router, jfield_GeneralRouter_rightTurn);
	rConfig.router.minDefaultSpeed = ienv->GetFloatField(router, jfield_GeneralRouter_minDefaultSpeed);
	rConfig.router.maxDefaultSpeed = ienv->GetFloatField(router, jfield_GeneralRouter_maxDefaultSpeed);
	// Map<String, String> attributes; // Attributes are not sync not used for calculation
	// Map<String, RoutingParameter> parameters;  // not used for calculation
	// Map<String, Integer> universalRules; // dynamically managed
	// List<String> universalRulesById // dynamically managed
	// Map<String, BitSet> tagRuleMask // dynamically managed
	// ArrayList<Object> ruleToValue // dynamically managed


	jobjectArray objectAttributes = (jobjectArray) ienv->GetObjectField(router, jfield_GeneralRouter_objectAttributes);
	for(int i = 0; i < ienv->GetArrayLength(objectAttributes); i++) {
		// RouteAttributeContext
		RouteAttributeContext* rctx = rConfig.router.newRouteAttributeContext();
		jobject ctx = ienv->GetObjectArrayElement(objectAttributes, i);
		jobjectArray ar = (jobjectArray) ienv->CallObjectMethod(ctx, jmethod_RouteAttributeContext_getParamKeys);
		vector<string> paramKeys = convertJArrayToStrings(ienv, ar);
		ienv->DeleteLocalRef(ar);
		ar = (jobjectArray) ienv->CallObjectMethod(ctx, jmethod_RouteAttributeContext_getParamValues);
		vector<string> paramValues = convertJArrayToStrings(ienv, ar);
		ienv->DeleteLocalRef(ar);

		rctx->registerParams(paramKeys, paramValues);
		
		jobjectArray rules = (jobjectArray) ienv->CallObjectMethod(ctx, jmethod_RouteAttributeContext_getRules);
		for(int j = 0; j < ienv->GetArrayLength(rules); j++) {
			RouteAttributeEvalRule* erule = rctx->newEvaluationRule();
			jobject rule = ienv->GetObjectArrayElement(rules, j);
			parseRouteAttributeEvalRule(ienv, rule, erule, &rConfig.router);
			ienv->DeleteLocalRef(rule);
		}
		ienv->DeleteLocalRef(rules);
		//printf("\n >>>>>>> %d \n", i + 1); rctx->printRules();

		ienv->DeleteLocalRef(ctx);
	}
	jlongArray impassableRoadIds = (jlongArray) ienv->CallObjectMethod(router, jmethod_GeneralRouter_getImpassableRoadIds);
	if(impassableRoadIds != NULL && ienv->GetArrayLength(impassableRoadIds) > 0) {
		jlong* iRi = (jlong*)ienv->GetLongArrayElements(impassableRoadIds, NULL);		
		for(int i = 0; i < ienv->GetArrayLength(impassableRoadIds); i++) {
			rConfig.router.impassableRoadIds.insert(iRi[i]);
		}
		ienv->ReleaseLongArrayElements(impassableRoadIds, (jlong*)iRi, 0);
	}
	
	ienv->DeleteLocalRef(impassableRoadIds);
	ienv->DeleteLocalRef(objectAttributes);
	ienv->DeleteGlobalRef(router);
	ienv->DeleteLocalRef(lrouter);	
	ienv->DeleteLocalRef(rName);

}

extern "C" JNIEXPORT jobjectArray JNICALL Java_net_osmand_NativeLibrary_nativeRouting(JNIEnv* ienv,
		jobject obj, jintArray  coordinates, jobject jRouteConfig, jfloat initDirection,
		jobjectArray regions, jobject progress, jobject precalculatedRoute, bool basemap,
		bool useSrRouting, jstring srDbPath, int srLevel) {
	RoutingConfiguration config(initDirection);
	parseRouteConfiguration(ienv, config, jRouteConfig);
	RoutingContext c(&config);
	c.progress = SHARED_PTR<RouteCalculationProgress>(new RouteCalculationProgressWrapper(ienv, progress));
	int* data = (int*)ienv->GetIntArrayElements(coordinates, NULL);
	c.startX = data[0];
	c.startY = data[1];
	c.targetX = data[2];
	c.targetY = data[3];
	c.basemap = basemap;
    c.useSrRouting = useSrRouting; // INFO set sr routing
	c.srDbPath = ienv->GetStringUTFChars(srDbPath, NULL); // INFO set sr db path
	c.srLevel = srLevel; // INFO set sr level
	parsePrecalculatedRoute(ienv, c, precalculatedRoute);
	ienv->ReleaseIntArrayElements(coordinates, (jint*)data, 0);
	vector<RouteSegmentResult> r = searchRouteInternal(&c, false);
	UNORDERED(map)<int64_t, int> indexes;
	for (int t = 0; t< ienv->GetArrayLength(regions); t++) {
		jobject oreg = ienv->GetObjectArrayElement(regions, t);
		int64_t fp = ienv->GetIntField(oreg, jfield_RouteRegion_filePointer);
		int64_t ln = ienv->GetIntField(oreg, jfield_RouteRegion_length);
		ienv->DeleteLocalRef(oreg);
		indexes[(fp <<31) + ln] = t;
	}

	// convert results
	jobjectArray res = ienv->NewObjectArray(r.size(), jclass_RouteSegmentResult, NULL);
	for (uint i = 0; i < r.size(); i++) {
		jobject resobj = convertRouteSegmentResultToJava(ienv, r[i], indexes, regions);
		ienv->SetObjectArrayElement(res, i, resobj);
		ienv->DeleteLocalRef(resobj);
	}
	if(c.finalRouteSegment.get() != NULL) {
		ienv->SetFloatField(progress, jfield_RouteCalculationProgress_routingCalculatedTime, c.finalRouteSegment->distanceFromStart);
	}
	ienv->SetIntField(progress, jfield_RouteCalculationProgress_visitedSegments, c.visitedSegments);
	ienv->SetIntField(progress, jfield_RouteCalculationProgress_loadedTiles, c.loadedTiles);
	if (r.size() == 0) {
		OsmAnd::LogPrintf(OsmAnd::LogSeverityLevel::Info, "No route found");
	}
	fflush(stdout);
	return res;
}

//	protected static native RouteDataObject[] getRouteDataObjects(NativeRouteSearchResult rs, int x31, int y31!);
extern "C" JNIEXPORT jobjectArray JNICALL Java_net_osmand_NativeLibrary_getRouteDataObjects(JNIEnv* ienv,
		jobject obj, jobject reg, jlong ref, jint x31, jint y31) {

	NativeRoutingTile* t = (NativeRoutingTile*) ref;
	uint64_t lr = ((uint64_t) x31 << 31) + y31;
	std::vector<RouteDataObject*> collected = t->cachedByLocations[lr];
	jobjectArray res = ienv->NewObjectArray(collected.size(), jclass_RouteDataObject, NULL);
	for (jint i = 0; i < (int)collected.size(); i++) {
		jobject robj = convertRouteDataObjectToJava(ienv, collected[i], reg);
		ienv->SetObjectArrayElement(res, i, robj);
		ienv->DeleteLocalRef(robj);
	}
	return res;
}

//protected static native NativeRouteSearchResult loadRoutingData(RouteRegion reg, String regName, int regfp, RouteSubregion subreg,
//			boolean loadObjects);
extern "C" JNIEXPORT jobject JNICALL Java_net_osmand_NativeLibrary_loadRoutingData(JNIEnv* ienv,
		jobject obj, jobject reg, jstring regName, jint regFilePointer,
		jobject subreg, jboolean loadObjects) {
	RoutingIndex ind;
	ind.filePointer = regFilePointer;
	ind.name = getString(ienv, regName);
	RouteSubregion sub(&ind);
	sub.filePointer = ienv->GetIntField(subreg, jfield_RouteSubregion_filePointer);
	sub.length = ienv->GetIntField(subreg, jfield_RouteSubregion_length);
	sub.left = ienv->GetIntField(subreg, jfield_RouteSubregion_left);
	sub.right = ienv->GetIntField(subreg, jfield_RouteSubregion_right);
	sub.top = ienv->GetIntField(subreg, jfield_RouteSubregion_top);
	sub.bottom = ienv->GetIntField(subreg, jfield_RouteSubregion_bottom);
	sub.mapDataBlock= ienv->GetIntField(subreg, jfield_RouteSubregion_shiftToData);
	std::vector<RouteDataObject*> result;
	SearchQuery q;
	searchRouteDataForSubRegion(&q, result, &sub);


	if (loadObjects) {
		jobjectArray res = ienv->NewObjectArray(result.size(), jclass_RouteDataObject, NULL);
		for (jint i = 0; i < (jint)result.size(); i++) {
			if (result[i] != NULL) {
				jobject robj = convertRouteDataObjectToJava(ienv, result[i], reg);
				ienv->SetObjectArrayElement(res, i, robj);
				ienv->DeleteLocalRef(robj);
			}
		}
		for (uint i = 0; i < result.size(); i++) {
			if (result[i] != NULL) {
				delete result[i];
			}
			result[i] = NULL;
		}
		return ienv->NewObject(jclass_NativeRouteSearchResult, jmethod_NativeRouteSearchResult_init, ((jlong) 0), res);
	} else {
		NativeRoutingTile* r = new NativeRoutingTile();
		for (jint i = 0; i < (int)result.size(); i++) {
			if (result[i] != NULL) {
				r->result.push_back(result[i]);
				for (jint j = 0; j < (int)result[i]->pointsX.size(); j++) {
					jint x = result[i]->pointsX[j];
					jint y = result[i]->pointsY[j];
					uint64_t lr = ((uint64_t) x << 31) + y;
					r->cachedByLocations[lr].push_back(result[i]);
				}
			}
		}
		jlong ref = (jlong) r;
		if(r->result.size() == 0) {
			ref = 0;
			delete r;
		}

		return ienv->NewObject(jclass_NativeRouteSearchResult, jmethod_NativeRouteSearchResult_init, ref, NULL);
	}

}


void pushToJavaRenderingContext(JNIEnv* env, jobject jrc, JNIRenderingContext* rc)
{
	env->SetIntField( jrc, jfield_RenderingContext_pointCount, (jint) rc->pointCount);
	env->SetIntField( jrc, jfield_RenderingContext_pointInsideCount, (jint)rc->pointInsideCount);
	env->SetIntField( jrc, jfield_RenderingContext_visible, (jint)rc->visible);
	env->SetIntField( jrc, jfield_RenderingContext_allObjects, rc->allObjects);
	env->SetIntField( jrc, jfield_RenderingContext_textRenderingTime, rc->textRendering.GetElapsedMs());
	env->SetIntField( jrc, jfield_RenderingContext_lastRenderedKey, rc->lastRenderedKey);
}

bool JNIRenderingContext::interrupted()
{
	return env->GetBooleanField(javaRenderingContext, jfield_RenderingContext_interrupted);
}

SkBitmap* JNIRenderingContext::getCachedBitmap(const std::string& bitmapResource) {
	JNIEnv* env = this->env;
	jstring jstr = env->NewStringUTF(bitmapResource.c_str());
	jbyteArray javaIconRawData = (jbyteArray)env->CallObjectMethod(this->javaRenderingContext, jmethod_RenderingContext_getIconRawData, jstr);
	env->DeleteLocalRef(jstr);
	if(!javaIconRawData)
		return NULL;

	jbyte* bitmapBuffer = env->GetByteArrayElements(javaIconRawData, NULL);
	jint bufferLen = env->GetArrayLength(javaIconRawData);

	// Decode bitmap
	SkBitmap* iconBitmap = new SkBitmap();
	//TODO: JPEG is badly supported! At the moment it needs sdcard to be present (sic). Patch that
	if(!SkImageDecoder::DecodeMemory(bitmapBuffer, bufferLen, iconBitmap))
	{
		// Failed to decode
		delete iconBitmap;

		this->nativeOperations.Start();
		env->ReleaseByteArrayElements(javaIconRawData, bitmapBuffer, JNI_ABORT);
		env->DeleteLocalRef(javaIconRawData);

		throwNewException(env, (std::string("Failed to decode ") + bitmapResource).c_str());

		return NULL;
	}

	env->ReleaseByteArrayElements(javaIconRawData, bitmapBuffer, JNI_ABORT);
	env->DeleteLocalRef(javaIconRawData);

	return iconBitmap;
}

std::string JNIRenderingContext::getTranslatedString(const std::string& name) {
	if (this->getPreferredLocale() == "en") {
		jstring n = this->env->NewStringUTF(name.c_str());
		jstring translate = (jstring) this->env->CallStaticObjectMethod(jclass_JUnidecode, jmethod_JUnidecode_unidecode, n);
		std::string res = getString(this->env, translate);
		this->env->DeleteLocalRef(translate);
		this->env->DeleteLocalRef(n);
		return res;
	}
	return name;
}

std::string JNIRenderingContext::getReshapedString(const std::string& name) {
	jstring n = this->env->NewStringUTF(name.c_str());
	jstring translate = (jstring) this->env->CallStaticObjectMethod(jclass_Reshaper, jmethod_Reshaper_reshape, n);
	std::string res = getString(this->env, translate);
	this->env->DeleteLocalRef(translate);
	this->env->DeleteLocalRef(n);
	return res;
}

