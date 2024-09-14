#include "main.hpp"

#include <cmath>
#include "scotland2/shared/modloader.h"
#include "bsml/shared/BSML-Lite.hpp"
#include "bsml/shared/BSML.hpp"

#include "HMUI/ViewController.hpp"
#include "HMUI/Touchable.hpp"

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};


Configuration &getConfig() {
  static Configuration config(modInfo);
  return config;
}

#define PI 3.14159265358979323846

// converts degrees to radians
float toRadians(float theta) {
  return theta * PI / 180.0f;
}

// does inverse tan then converts to degrees
float toDegrees(float tanFov) {
  return std::atan(tanFov) * 180.0 / PI;
}

// converts into radians and calculates the tan
float toTan(float degFov) {
  return std::tan(toRadians(degFov));
}

struct Vector4 {
  float x;
  float y;
  float z;
  float w;
};

struct Matrix4x4 {
  Vector4 columns[4];
};


const float defaultFOV = 120.0f;

float userSetFov = -1.0f;

// mod settings

void setFOV(float fov) {
  userSetFov = fov;
  getConfig().config["fov"].SetFloat(fov);
  getConfig().Write();
}

float getCurrentFOV() {
  if(userSetFov < 0) {
    Configuration& config = getConfig();
    if(config.config.HasMember("fov")) {
      userSetFov = config.config["fov"].GetFloat();
    } else  {
      config.config.AddMember("fov", defaultFOV, config.config.GetAllocator());
      userSetFov = defaultFOV;
    }
  }

  return userSetFov;
}

struct FovParams {
  float zNear;
  float zFar;
  float upTan;
  float downTan;
  float leftTan;
  float rightTan;
  float normalHorizFovDegrees;
};

FovParams& getDefaultFovParams() {
  static FovParams defaultParams;
  static bool loaded;
  if(!loaded) {
    void* libOvrPlugin = dlopen("libOVRPlugin.so", 0);

    int (*getNodeFrustum2)(int, FovParams*) = (int(*)(int, FovParams*)) dlsym(libOvrPlugin, "ovrp_GetNodeFrustum2");
  
    getNodeFrustum2(0, &defaultParams);
    dlclose(libOvrPlugin);

    defaultParams.normalHorizFovDegrees = toDegrees(defaultParams.leftTan) + toDegrees(defaultParams.rightTan);
  }

  return defaultParams;
}

void ModSettings_DidActivate(HMUI::ViewController* self, bool firstActivation, bool addedToHierarchy, bool systemScreenEnabling) {
  if(firstActivation) {
    self->get_gameObject()->AddComponent<HMUI::Touchable*>();
    
    auto* container = BSML::Lite::CreateScrollableSettingsContainer(self);
    auto* setting = BSML::Lite::CreateSliderSetting(container, "FOV", 1.0f, getCurrentFOV(), 30.0f, 170.0f, setFOV);
    BSML::Lite::CreateUIButton(container, "Reset", [setting]{
      float normalFov = getDefaultFovParams().normalHorizFovDegrees;
      setting->set_Value(normalFov);
      setFOV(normalFov);
    });
  }
}

// makes uh.. a perspective projection matrix
// don't ask me how it works, I copied it from a ghidra decomp 💀
Matrix4x4 makeProjectionMatrix(float zNear,
  float zFar,
  float upTan,
  float downTan,
  float leftTan,
  float rightTan) {
  float fVar1 = zNear;
  float fVar3 = zFar;
  float fVar7 = fVar1 * leftTan + fVar1 * rightTan;
  float fVar5 = fVar1 * upTan + fVar1 * downTan;
  float fVar4 = (fVar1 + fVar1) / fVar7;
  fVar7 = (fVar1 * rightTan - fVar1 * leftTan) / fVar7;
  float fVar6 = (fVar1 * upTan - fVar1 * downTan) / fVar5;
  float fVar2 = -(fVar1 * (fVar3 + fVar3)) / (fVar3 - fVar1);
  Matrix4x4 matrix;
  memset(&matrix, 0, sizeof(Matrix4x4));
  matrix.columns[2].w = -1.0;
  matrix.columns[0].x = fVar4;
  matrix.columns[1].y = (fVar1 + fVar1) / fVar5;
  matrix.columns[2].x = fVar7;
  matrix.columns[2].y = fVar6;
  matrix.columns[2].z = -(fVar1 + fVar3) / (fVar3 - fVar1);
  matrix.columns[3].z = fVar2;
  matrix.columns[3].w = 0.0;

  return matrix;
}

int (*PopulateNextFrameDescOrig)(void*, void*, char*);
int PopulateNextFrameDesc(void* self, void* frameHints, char* nextFrame) {
  int status = PopulateNextFrameDescOrig(self, frameHints, nextFrame);

  // overwrite the projection matrix set by PopulateNextFrameDescOrig with our own

  // cba to write out all the definitions so cursed pointer arithmetic
  char* renderPass = nextFrame;
  char* renderParamsLeftEye = renderPass + 0x18;
  char* renderParamsRightEye = renderParamsLeftEye + 0x78;
  char* leftEyeProjection = renderParamsLeftEye + 0x1c;
  char* rightEyeProjection = renderParamsRightEye + 0x1c;
  Matrix4x4* leftEyeMatrix = (Matrix4x4*) (leftEyeProjection + 0x4);
  Matrix4x4* rightEyeMatrix = (Matrix4x4*) (rightEyeProjection + 0x4);

  FovParams& params = getDefaultFovParams();
  float upTan = params.upTan;
  float downTan = params.downTan;
  float leftTan = params.leftTan;
  float rightTan = params.rightTan;

  // calculate the new FOV based upon the mod settings.
  float fovDifference = getCurrentFOV() - params.normalHorizFovDegrees;
  leftTan = toTan(toDegrees(leftTan) + fovDifference * 0.5f);
  rightTan = toTan(toDegrees(rightTan) + fovDifference * 0.5f);

  // make a projection matrix for each eye based on the FOV
  *leftEyeMatrix = makeProjectionMatrix(params.zNear, params.zFar, upTan, downTan, leftTan, rightTan);
  // don't ask me why the left/right FOV is different, I don't fucking know
  *rightEyeMatrix = makeProjectionMatrix(params.zNear, params.zFar, upTan, downTan, rightTan, leftTan);
  
  return status;
}

MOD_EXTERN_FUNC void setup(CModInfo *info) noexcept {
  *info = modInfo.to_c();

  getConfig().Load();
  
  Paper::Logger::RegisterFileContextId(PaperLogger.tag);
}

MOD_EXTERN_FUNC void late_load() noexcept {
  il2cpp_functions::Init();
  BSML::Register::RegisterSettingsMenu("FOV Mod", ModSettings_DidActivate, false);

  // do a raw offset hook

  // error handling? what's that
  void* libOvrXrPlugin = dlopen("libOculusXRPlugin.so", 0);
  // dlsym for a symbol that we know the offset of
  char* enableAppMetrics = (char*) dlsym(libOvrXrPlugin, "EnableAppMetrics");
  char* baseAddr = enableAppMetrics - 0x00110b4c; // subtract known offset to get base addr.
  char* hookingAddr = baseAddr + 0x001143e8; // add offset of method to hook
  dlclose(libOvrXrPlugin);

  A64HookFunction((void*) hookingAddr, (void*) PopulateNextFrameDesc, (void**) &PopulateNextFrameDescOrig);
  PaperLogger.info("Installed PopulateNextFrameDesc hook");
}