#include <fstream>
#include <boost/algorithm/string/predicate.hpp>

#include "ao/render/brep/contours.hpp"
#include "ao/render/brep/xtree.hpp"
#include "ao/render/brep/dual.hpp"
#include "ao/render/brep/brep.hpp"

namespace Kernel {

class Segments : public BRep<2>
{
public:
    void operator()(const std::array<const XTree<2>*, 2>& ts)
    {
        uint32_t vs[2];
        for (unsigned i=0; i < ts.size(); ++i)
        {
            if (ts[i]->index == 0)
            {
                ts[i]->index = verts.size();
                verts.push_back(ts[i]->vert.template cast<float>());
            }
            vs[i] = ts[i]->index;
        }
        branes.push_back({vs[0], vs[1]});
    }
};

std::unique_ptr<Contours> Contours::render(const Tree t, const Region<2>& r,
                                           double min_feature)
{
    // Create the quadtree on the scaffold
    auto xtree = XTree<2>::build(t, r, min_feature);

    // Perform marching squares
    Segments segs;
    Dual<2>::walk(xtree.get(), segs);

    auto c = std::unique_ptr<Contours>(new Contours(r));

    // Maps from index (in ms) to item in segments vector
    std::map<uint32_t, uint32_t> heads;
    std::map<uint32_t, uint32_t> tails;
    std::vector<std::list<uint32_t>> contours;

    for (auto& s : segs.branes)
    {
        {   // Check to see whether we can attach to the back of a tail
            auto t = tails.find(s[0]);
            if (t != tails.end())
            {
                contours[t->second].push_back(s[1]);
                tails.insert({s[1], t->second});
                tails.erase(t);
                continue;
            }
        }

        {   // Otherwise, see if we should prepend ourselves to a head
            auto h = heads.find(s[1]);
            if (h != heads.end())
            {
                contours[h->second].push_front(s[0]);
                heads.insert({s[0], h->second});
                heads.erase(h);
                continue;
            }
        }

        // Otherwise, start a new multi-segment line
        heads[s[0]] = contours.size();
        tails[s[1]] = contours.size();
        contours.push_back({s[0], s[1]});
    }

    std::vector<bool> processed(contours.size(), false);
    for (unsigned i=0; i < contours.size(); ++i)
    {
        if (processed[i])
        {
            continue;
        }
        c->contours.push_back(std::vector<Eigen::Vector2f>());

        // Weld multiple contours together here
        unsigned target = i;
        while (true)
        {
            for (const auto& pt : contours[target])
            {
                c->contours.back().push_back(segs.verts[pt]);
            }
            processed[target] = true;

            // Check for the next potentially-connected contour
            auto h = heads.find(contours[target].back());
            if (h != heads.end() && !processed[h->second])
            {
                // Remove the back point, beause it will be re-inserted
                // as the first point in the next contour
                c->contours.back().pop_back();
                target = h->second;
            }
            else
            {
                break;
            }
        }
    }

    return c;
}

bool Contours::saveSVG(const std::string& filename)
{
    if (!boost::algorithm::iends_with(filename, ".svg"))
    {
        std::cerr << "Contours::saveSVG: filename \"" << filename
                  << "\" does not end in .svg" << std::endl;
    }
    std::ofstream file;
    file.open(filename, std::ios::out);
    if (!file.is_open())
    {
        std::cout << "Contours::saveSVG: could not open " << filename
                  << std::endl;
        return false;
    }

    file <<
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\"\n"
          " width=\"" << bbox.upper.x() - bbox.lower.x() <<
        "\" height=\"" << bbox.upper.y() - bbox.lower.y() <<
        "\" id=\"Ao\">\n";

    for (const auto& seg : contours)
    {
        file << "<path d=\"";

        const bool closed = seg.front() == seg.back();
        auto itr = seg.cbegin();
        auto end = seg.cend();
        if (closed)
        {
            end--;
        }

        // Initial move command
        file << "M " << itr->x() - bbox.lower.x()
             << ' '  << bbox.upper.y() - itr->y() << ' ';
        itr++;

        // Line to commands
        while (itr != end)
        {
            file << "L " << itr->x() - bbox.lower.x()
                 << ' '  << bbox.upper.y() - itr->y() << ' ';
            ++itr;
        }

        if (closed)
        {
            file << "Z";
        }
        file << "\"\nfill=\"none\" stroke=\"black\" stroke-width=\"0.01\"/>";
    }
    file << "\n</svg>";
    return true;
}


}   // namespace Kernel