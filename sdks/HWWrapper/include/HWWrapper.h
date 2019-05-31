//-------------------------------------------------------------------------
// Copyright © 2019 Apple Inc. All rights reserved.
//-------------------------------------------------------------------------

#pragma once
#include "glm/fwd.hpp"

namespace HWW
{
    // Forward declare manager class.
    class Manager;

    class __declspec(dllexport) HWWrapper
    {
    public:
        HWWrapper(int argc, const char* argv[]);
        ~HWWrapper();

        // Non-copyable.
        HWWrapper(const HWWrapper&) = delete;
        HWWrapper(const HWWrapper&&) = delete;
        HWWrapper& operator=(const HWWrapper&) = delete;
        HWWrapper&& operator=(const HWWrapper&&) = delete;

        // Set IPD in mm.
        void SetIPD(float ipd) const;

        // Initializes the wrapper.
        // Must be called after GLFW window has been created.
        bool Initialize() const;

        // Set Tracker prediction time in seconds.
        void SetTrackerPredictionTime(float predictionTimeInS) const;

        // Get Position and Orientation of the headset.
        // Returns true if successful, false if pose was invalid or error occured.
        bool GetHMDPose(glm::vec3& outPosition, glm::quat& outOrientation) const;

        // Set width and height of full viewport.
        void SetViewportDimentions(int width, int height) const;
        
        // Render textures from buffer defined with the data format: GL_UNSIGNED_INT_8_8_8_8, and texture format: GL_BGRA.
        // If only one of the buffers contain data, that buffer will be used to render both left and right eyes.
        // Requires wrapper to be initialized by calling Initialize().
        void Render(const unsigned* leftEyeTextureData, const unsigned* rightEyeTextureData, int textureWidth, int textureHeight, float audioPresentationTime) const;
        
        // Render OpenGL textures (GLuint).
        // If only one of the textureID contain valid data, that textureID will be used to render both left and right eyes.
        // Requires wrapper to be initialized by calling Initialize().
        void Render(unsigned int leftEyeTextureID, unsigned int rightEyeTextureID, float audioPresentationTime) const;

        // The transformation matrix for the left eye.
        // Projection * Eye^-1 matrix, should be recalculated when IPD is updated.
        const glm::mat4 GetLeftEyeTransformationMatrix(float zNear, float zFar) const;

        // The transformation matrix for the right eye.
        // Projection * Eye^-1 matrix, should be recalculated when IPD is updated.
        const glm::mat4 GetRightEyeTransformationMatrix(float zNear, float zFar) const;

    private:
        Manager* m_pHwwManager;
    };
}
