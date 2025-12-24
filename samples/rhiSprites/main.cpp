#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/SpriteSystem.hpp"

#include "../common/RhiSampleApp.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/random.hpp>
#include <imgui.h>

using namespace pnkr;

class SpriteSample : public samples::RhiSampleApp
{
public:
    SpriteSample()
        : samples::RhiSampleApp({
            .title = "PNKR - Sprites / Billboards",
            .width = 1280,
            .height = 720,
            .createRenderer = true
        })
    {
    }

    std::unique_ptr<renderer::scene::SpriteSystem> m_sprites;
    renderer::scene::Camera m_camera;
    std::vector<TextureHandle> m_flipbookFrames;

    uint32_t m_totalSprites = 0;
    uint32_t m_alphaSprites = 0;
    uint32_t m_additiveSprites = 0;
    uint32_t m_premulSprites = 0;

    void onInit() override
    {
        m_sprites = std::make_unique<renderer::scene::SpriteSystem>(*m_renderer);

        // Load flipbook textures
        m_flipbookFrames.push_back(m_renderer->loadTexture(baseDir() / "assets" / "explosion_256_f00.png", true));
        m_flipbookFrames.push_back(m_renderer->loadTexture(baseDir() / "assets" / "explosion_256_f01.png", true));
        m_flipbookFrames.push_back(m_renderer->loadTexture(baseDir() / "assets" / "explosion_256_f02.png", true));
        m_flipbookFrames.push_back(m_renderer->loadTexture(baseDir() / "assets" / "explosion_256_f03.png", true));

        TextureHandle staticTex = m_flipbookFrames.front();

        // Static billboard at origin
        renderer::scene::Sprite billboard{};
        billboard.space = renderer::scene::SpriteSpace::WorldBillboard;
        billboard.position = {0.0f, 0.0f, 0.0f};
        billboard.size = {1.0f, 1.0f};
        billboard.texture = staticTex;
        m_sprites->createSprite(billboard);

        // Flipbook billboard
        auto clip = m_sprites->createFlipbookClip(m_flipbookFrames, 10.0f, true);
        renderer::scene::Sprite flip{};
        flip.space = renderer::scene::SpriteSpace::WorldBillboard;
        flip.position = {1.5f, 0.0f, 0.0f};
        flip.size = {1.0f, 1.0f};
        flip.clip = clip;
        m_sprites->createSprite(flip);

        // Screen-space sprite
        renderer::scene::Sprite screen{};
        screen.space = renderer::scene::SpriteSpace::Screen;
        screen.position = {50.0f, 50.0f, 0.0f};
        screen.size = {128.0f, 128.0f};
        screen.pivot = {0.0f, 0.0f};
        screen.texture = staticTex;
        m_sprites->createSprite(screen);

        // Blend mode showcase
        renderer::scene::Sprite alpha = screen;
        alpha.position = {300.0f, 200.0f, 0.0f};
        alpha.color = {1.0f, 1.0f, 1.0f, 0.8f};
        alpha.blend = renderer::scene::SpriteBlendMode::Alpha;
      //  m_sprites->createSprite(alpha);

        renderer::scene::Sprite additive = alpha;
        additive.position = {340.0f, 220.0f, 0.0f};
        additive.color = {0.3f, 0.8f, 1.0f, 0.6f};
        additive.blend = renderer::scene::SpriteBlendMode::Additive;
        m_sprites->createSprite(additive);

        renderer::scene::Sprite premul = alpha;
        premul.position = {380.0f, 240.0f, 0.0f};
        premul.color = {1.0f, 0.6f, 0.3f, 0.7f};
        premul.blend = renderer::scene::SpriteBlendMode::Premultiplied;
       // m_sprites->createSprite(premul);

        // Stress test batch
        constexpr uint32_t kStressCount = 1;
        for (uint32_t i = 0; i < kStressCount; ++i)
        {
            renderer::scene::Sprite s{};
            s.space = renderer::scene::SpriteSpace::WorldBillboard;
            s.position = glm::linearRand(glm::vec3(-25.0f, -10.0f, -10.0f),
                                         glm::vec3(25.0f, 10.0f, 10.0f));
            s.size = {0.25f, 0.25f};
            s.texture = staticTex;
            s.color = {0.8f, 0.9f, 1.0f, 0.8f};
            s.blend = renderer::scene::SpriteBlendMode::Alpha;
         //   m_sprites->createSprite(s);
        }

        m_totalSprites = kStressCount + 6;
        m_alphaSprites = kStressCount + 2;
        m_additiveSprites = 1;
        m_premulSprites = 1;

        m_camera.lookAt({0.0f, 1.0f, 6.0f}, {0.0f, 0.0f, 0.0f});

        initUI();
    }

    void onUpdate(float dt) override
    {
        float aspect = static_cast<float>(m_window.width()) / m_window.height();
        m_camera.setPerspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

        if (m_sprites)
        {
            m_sprites->update(dt);
        }
    }

    void onRecord(const renderer::RHIFrameContext& ctx) override
    {
        // Debug hack: force wait to avoid sync hazards in RHIRenderer
        m_renderer->device()->waitIdle();

        if (m_sprites)
        {
            m_sprites->render(ctx.commandBuffer, m_camera, m_window.width(), m_window.height(), ctx.frameIndex);
        }
    }

    void onEvent(const SDL_Event& event) override
    {
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            m_renderer->resize(event.window.data1, event.window.data2);
        }
    }

    void onImGui() override
    {
        ImGui::Text("Sprites: %u (alpha %u, additive %u, premul %u)",
                    m_totalSprites, m_alphaSprites, m_additiveSprites, m_premulSprites);
        ImGui::Text("Expected draw calls: <= 3 (one per blend mode)");
    }
};

int main(int argc, char** argv)
{
    SpriteSample app;
    return app.run();
}
