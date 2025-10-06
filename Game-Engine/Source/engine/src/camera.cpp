#include <camera.h>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

static const float SENSITIVITY = 400.f;

void Camera::update() {
	glm::mat4 cameraRotation = getRotationMatrix();
	position += glm::vec3(cameraRotation * glm::vec4(velocity * 0.2f, 0.f));
}

void Camera::processSDLEvents(SDL_Event& event) {
	switch (event.type) {
	case SDL_KEYDOWN: keyInput(event.key.keysym.sym); break;
	case SDL_KEYUP: keyInput(event.key.keysym.sym, false); break;
	case SDL_MOUSEMOTION: 
		yaw += std::clamp((float)event.motion.xrel / SENSITIVITY, -(float)M_PI / 2, (float)M_PI / 2);
		pitch -= (float)event.motion.yrel / SENSITIVITY;
		break;
	}
}

void Camera::keyInput(SDL_Keycode key, bool reset) {
	switch (key) {
	case SDLK_w: velocity.z = reset ? -1 : 0; break;
	case SDLK_a: velocity.x = reset ? -1 : 0; break;
	case SDLK_s: velocity.z = reset ? 1 : 0; break;
	case SDLK_d: velocity.x = reset ? 1 : 0; break;
	}
}

glm::mat4 Camera::getViewMatrix() {
	glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
	glm::mat4 cameraRotation = getRotationMatrix();
	return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix() {
	glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3{ 1.f, 0.f, 0.f });
	glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3{ 0.f, -1.f, 0.f });

	return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}


