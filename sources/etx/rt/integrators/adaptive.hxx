#pragma once

#include "integrator.hxx" // Assuming base Integrator is here
#include "sampler/sampler.hxx" // Assuming a Sampler is used for generating samples

namespace etx {
namespace rt {

/**
 * @brief An Integrator implementing Adaptive Path Tracing.
 * * This integrator uses a variance-based approach to allocate more samples 
 * to noisy pixels and stop early in low-variance areas.
 */
class AdaptiveIntegrator : public Integrator {
public:
    // Constructor
    AdaptiveIntegrator(std::shared_ptr<const Scene> p_scene);

    // Core method to compute radiance for a single ray (Li = Light)
    float3 Li(const Ray& r, const Sample& s, Hit& h) const override;
    
    // The main rendering loop that handles pixel-wise adaptive sampling
    void render(std::shared_ptr<FrameBuffer> p_fb) override;

private:
    // Configuration parameters for the adaptive process
    float m_variance_threshold = 0.001f; // Threshold to stop sampling a pixel
    int m_max_spp = 256;                 // Maximum samples per pixel
    int m_initial_spp = 8;               // Initial samples before checking variance
    int m_batch_size = 8;                // Number of samples to add per adaptive batch

    // Helper function (e.g., to trace a single path for a given sample)
    float3 trace_path(const Ray& primary_ray, const Sample& s, Hit& h) const;
};

} // namespace rt
} // namespace etx
