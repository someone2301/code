#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <algorithm>

// Procedural lightning shared by the hammer and the EQ nodes.
namespace kvfx
{
    // Adds a jagged bolt to `path`: from p along `angle`, `segments` steps, sideways jitter `jag`, with forks
    // (branchDepth levels). Points are kept inside `room`.
    inline void addJagged (juce::Path& path, juce::Random& rng, juce::Point<float> p, float angle, float length, int segments,
                           float jag, int branchDepth, juce::Rectangle<float> room, float forkChance = 0.38f)
    {
        auto randIn = [&rng] (float lo, float hi) { return lo + (hi - lo) * rng.nextFloat(); };
        path.startNewSubPath (p);
        const float step = length / (float) juce::jmax (1, segments);
        for (int k = 0; k < segments; ++k)
        {
            angle += randIn (-0.55f, 0.55f);
            const juce::Point<float> dir { std::cos (angle), std::sin (angle) };
            const juce::Point<float> nrm { -dir.y, dir.x };
            p = p + dir * step + nrm * randIn (-jag, jag);
            p = { juce::jlimit (room.getX(), room.getRight(), p.x), juce::jlimit (room.getY(), room.getBottom(), p.y) };
            path.lineTo (p);
            if (branchDepth > 0 && k < segments - 1 && rng.nextFloat() < forkChance)
            {
                juce::Path branch;
                addJagged (branch, rng, p, angle + (rng.nextBool() ? -1.0f : 1.0f) * randIn (0.35f, 0.9f),
                           length * randIn (0.25f, 0.5f), juce::jmax (2, segments / 2), jag * 0.8f, branchDepth - 1, room, forkChance);
                path.addPath (branch);
                path.startNewSubPath (p);   // continue the trunk from the fork
            }
        }
    }

    // Strokes a bolt: wide faint glow, coloured body, white core.
    inline void strokeBolt (juce::Graphics& g, const juce::Path& p, juce::Colour glow, juce::Colour core, float width, float alpha)
    {
        const auto st = [] (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded); };
        g.setColour (glow.withAlpha (0.16f * alpha));
        g.strokePath (p, st (width * 6.0f));
        g.setColour (glow.withAlpha (0.7f * alpha));
        g.strokePath (p, st (width * 2.4f));
        g.setColour (core.withAlpha (alpha));
        g.strokePath (p, st (width * 0.9f));
    }

    // Glossy orb for an EQ node: lit from the upper left, coloured body, darker rim, specular highlight.
    inline void paintOrb (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, bool bright, bool on)
    {
        const auto body = on ? colour : juce::Colour (0xff5d6a88);
        // soft halo
        juce::ColourGradient halo (body.withAlpha (bright ? 0.45f : 0.22f), c.x, c.y, body.withAlpha (0.0f), c.x + r * 2.0f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (r * 4.0f, r * 4.0f).withCentre (c));
        // sphere
        juce::ColourGradient sphere (juce::Colour (0xfff4f7fc).withAlpha (bright ? 0.95f : 0.75f), c.x - r * 0.35f, c.y - r * 0.4f,
                                     body.darker (0.9f), c.x + r * 0.9f, c.y + r * 0.9f, true);
        sphere.addColour (0.35, body.brighter (bright ? 0.25f : 0.0f));
        sphere.addColour (0.8, body.darker (0.35f));
        g.setGradientFill (sphere);
        g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c));
        // rim and specular highlight
        g.setColour (body.brighter (0.6f).withAlpha (0.9f));
        g.drawEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (bright ? 0.85f : 0.6f));
        g.fillEllipse (juce::Rectangle<float> (r * 0.7f, r * 0.42f).withCentre ({ c.x - r * 0.32f, c.y - r * 0.5f }));
    }

    // Plasma-ball lightning on the selected EQ node: arcs crawling over the orb's surface and filaments inside it, all
    // kept within a few pixels of the orb. Bolts are stored relative to the node's centre, so they follow it while it
    // is dragged.
    class NodeLightning
    {
    public:
        static constexpr float shell = 6.0f;   // how far outside the orb the arcs may reach

        void tick (double nowMs, float radius)
        {
            const double dt = lastMs > 0.0 ? juce::jlimit (0.0, 100.0, nowMs - lastMs) : 16.0;
            lastMs = nowMs;
            bolts.erase (std::remove_if (bolts.begin(), bolts.end(), [&] (const Bolt& b) { return nowMs - b.born > b.life; }), bolts.end());
            acc += (float) (dt * 0.001) * 70.0f * (0.5f + rng.nextFloat());
            while (acc >= 1.0f && bolts.size() < 18)
            {
                acc -= 1.0f;
                spawn (nowMs, radius);
            }
            if (nowMs >= nextSurge)
            {
                flash = 1.0f;
                for (int k = 0; k < 3; ++k) spawn (nowMs, radius);
                nextSurge = nowMs + 300.0 + 700.0 * rng.nextFloat();
            }
            flash = std::max (0.0f, flash - (float) dt * 0.005f);
        }

        void paint (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, double nowMs)
        {
            place (centre);
            const auto white = juce::Colour (0xfff4f7fc);
            g.saveState();
            juce::Path clip;
            clip.addEllipse (juce::Rectangle<float> (2.0f * (radius + shell + 1.5f), 2.0f * (radius + shell + 1.5f)).withCentre (centre));
            g.reduceClipRegion (clip);
            // charged shell: flickers and flashes with each surge
            const float f = 0.55f + 0.45f * rng.nextFloat() + 0.6f * flash;
            juce::ColourGradient shellG (colour.withAlpha (0.0f), centre.x, centre.y, colour.withAlpha (juce::jmin (1.0f, 0.35f * f)),
                                         centre.x + radius + shell, centre.y, true);
            shellG.addColour (radius / (radius + shell), colour.withAlpha (0.0f));
            g.setGradientFill (shellG);
            g.fillEllipse (juce::Rectangle<float> (2.0f * (radius + shell), 2.0f * (radius + shell)).withCentre (centre));
            for (auto& b : bolts)
            {
                const float age = (float) ((nowMs - b.born) / b.life);
                float alpha = std::sqrt (juce::jlimit (0.0f, 1.0f, 1.0f - age));
                alpha *= rng.nextFloat() < 0.18f ? 0.3f : 0.75f + 0.25f * rng.nextFloat();
                strokeBolt (g, b.path, colour, white, b.width, alpha);
            }
            g.restoreState();
        }

        // Bolts translated to the node's centre (call before paint).
        void place (juce::Point<float> centre)
        {
            for (auto& b : bolts)
                if (b.at != centre)
                {
                    b.path.applyTransform (juce::AffineTransform::translation (centre - b.at));
                    b.at = centre;
                }
            last = centre;
        }

        int activeBolts() const noexcept { return (int) bolts.size(); }

    private:
        struct Bolt { juce::Path path; juce::Point<float> at; double born = 0, life = 100; float width = 1.0f; };

        void spawn (double nowMs, float radius)
        {
            Bolt b;
            b.born = nowMs;
            b.at = last;
            auto jit = [this] (float a) { return (rng.nextFloat() * 2.0f - 1.0f) * a; };
            if (rng.nextFloat() < 0.6f)
            {
                // arc crawling over the surface, inside the shell
                const float a0 = juce::MathConstants<float>::twoPi * rng.nextFloat();
                const float sweep = (rng.nextBool() ? 1.0f : -1.0f) * (0.6f + 1.2f * rng.nextFloat());
                constexpr int steps = 7;
                for (int k = 0; k <= steps; ++k)
                {
                    const float a = a0 + sweep * (float) k / steps;
                    const float rr = radius + 1.0f + std::abs (jit (shell - 1.0f));
                    const juce::Point<float> p = last + juce::Point<float> (std::cos (a), std::sin (a)) * rr;
                    if (k == 0) b.path.startNewSubPath (p); else b.path.lineTo (p);
                }
                b.width = 0.8f + 0.4f * rng.nextFloat();
            }
            else
            {
                // filament from near the centre out to the surface (plasma ball)
                const float a = juce::MathConstants<float>::twoPi * rng.nextFloat();
                const juce::Point<float> dir (std::cos (a), std::sin (a));
                const juce::Point<float> nrm (-dir.y, dir.x);
                constexpr int steps = 5;
                for (int k = 0; k <= steps; ++k)
                {
                    const float t = (float) k / steps;
                    const juce::Point<float> p = last + dir * (radius * (0.15f + 0.9f * t)) + nrm * jit (radius * 0.22f * (k == 0 || k == steps ? 0.3f : 1.0f));
                    if (k == 0) b.path.startNewSubPath (p); else b.path.lineTo (p);
                }
                b.width = 0.6f + 0.3f * rng.nextFloat();
            }
            b.life = 45.0 + 90.0 * rng.nextFloat();
            bolts.push_back (std::move (b));
        }

        std::vector<Bolt> bolts;
        juce::Random rng;
        juce::Point<float> last;
        double lastMs = 0, nextSurge = 0;
        float acc = 0, flash = 0;
    };
}
