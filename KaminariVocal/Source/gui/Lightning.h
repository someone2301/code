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

    // Electricity crackling around a round node (the selected EQ band). Bolts are stored relative to the node's
    // centre, so they follow it while it is dragged.
    class NodeLightning
    {
    public:
        void tick (double nowMs, float radius)
        {
            const double dt = lastMs > 0.0 ? juce::jlimit (0.0, 100.0, nowMs - lastMs) : 16.0;
            lastMs = nowMs;
            bolts.erase (std::remove_if (bolts.begin(), bolts.end(), [&] (const Bolt& b) { return nowMs - b.born > b.life; }), bolts.end());
            acc += (float) (dt * 0.001) * 75.0f * (0.5f + rng.nextFloat());
            while (acc >= 1.0f && bolts.size() < 32)
            {
                acc -= 1.0f;
                spawn (nowMs, radius, false);
            }
            if (nowMs >= nextSurge)
            {
                for (int k = 0; k < 2 + rng.nextInt (3); ++k) spawn (nowMs, radius, true);
                flash = 1.0f;
                nextSurge = nowMs + 350.0 + 900.0 * rng.nextFloat();
            }
            flash = std::max (0.0f, flash - (float) dt * 0.004f);
        }

        void paint (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, double nowMs)
        {
            place (centre);
            const auto white = juce::Colour (0xfff4f7fc);
            // flickering charged ring
            const float f = 0.6f + 0.4f * rng.nextFloat() + 0.5f * flash;
            g.setColour (colour.withAlpha (juce::jmin (1.0f, 0.22f * f)));
            g.fillEllipse (juce::Rectangle<float> (2.0f * (radius + 9.0f), 2.0f * (radius + 9.0f)).withCentre (centre));
            g.setColour (white.withAlpha (juce::jmin (1.0f, 0.5f * f)));
            g.drawEllipse (juce::Rectangle<float> (2.0f * (radius + 2.5f), 2.0f * (radius + 2.5f)).withCentre (centre), 1.2f);
            for (auto& b : bolts)
            {
                const float age = (float) ((nowMs - b.born) / b.life);
                float alpha = std::sqrt (juce::jlimit (0.0f, 1.0f, 1.0f - age));
                alpha *= rng.nextFloat() < 0.18f ? 0.25f : 0.75f + 0.25f * rng.nextFloat();
                strokeBolt (g, b.path, colour, white, b.width, alpha);
            }
        }

        // Bolts translated to the node's centre (call before paint).
        void place (juce::Point<float> centre)
        {
            for (auto& b : bolts)
            {
                if (b.at != centre)
                {
                    b.path.applyTransform (juce::AffineTransform::translation (centre - b.at));
                    b.at = centre;
                }
            }
            last = centre;
        }

        int activeBolts() const noexcept { return (int) bolts.size(); }

    private:
        struct Bolt { juce::Path path; juce::Point<float> at; double born = 0, life = 100; float width = 1.0f; };

        void spawn (double nowMs, float radius, bool surge)
        {
            Bolt b;
            b.born = nowMs;
            b.at = last;
            const float a = juce::MathConstants<float>::twoPi * rng.nextFloat();
            const juce::Point<float> start = last + juce::Point<float> (std::cos (a), std::sin (a)) * (radius + 1.0f);
            // mostly outward, bent along the ring so arcs crawl around the node
            const float dir = a + (rng.nextBool() ? 1.0f : -1.0f) * (0.3f + 0.9f * rng.nextFloat());
            const juce::Rectangle<float> room (last.x - 80.0f, last.y - 80.0f, 160.0f, 160.0f);
            addJagged (b.path, rng, start, dir, radius * (surge ? 3.0f + 2.0f * rng.nextFloat() : 1.1f + 1.4f * rng.nextFloat()),
                       surge ? 6 + rng.nextInt (3) : 3 + rng.nextInt (3), surge ? 3.0f : 2.0f, surge ? 1 : 0, room);
            b.life = surge ? 160.0 + 180.0 * rng.nextFloat() : 70.0 + 110.0 * rng.nextFloat();
            b.width = surge ? 1.3f : 0.8f + 0.4f * rng.nextFloat();
            bolts.push_back (std::move (b));
        }

        std::vector<Bolt> bolts;
        juce::Random rng;
        juce::Point<float> last;
        double lastMs = 0, nextSurge = 0;
        float acc = 0, flash = 0;
    };
}
