#include "core/xur.h"

#include <stdio.h>
#include <string.h>
#include <map>

namespace xur {

namespace {

struct SchemaProp
{
    const char* name;
    int type;
    int flags;
    int sub;            // first sub-property (kSchemaProps index)
    int subCount;
};

struct SchemaClass
{
    const char* name;
    const char* base;   // "" for XuiElement
    int first;
    int count;
};

#include "core/xur_schema.inc"

enum { kBool = 1, kString = 5, kObject = 9 };

// A property list as the walker sees it: pointers into kSchemaProps, or a
// custom class's types (no sub-properties).
struct Level
{
    std::string cls;
    std::vector<const SchemaProp*> props;
    std::vector<int> types;
    bool control;       // XuiControl, as 1888 stored it

    size_t Count() const { return props.empty() ? types.size() : props.size(); }
    int Type(size_t i) const { return props.empty() ? types[i] : props[i]->type; }
};

struct Node
{
    std::string cls;
    unsigned id;                    // STRN index of its Id, 0 if none
    std::vector<Node*> children;
    ~Node()
    {
        for (size_t i = 0; i < children.size(); i++)
            delete children[i];
    }
};

const SchemaClass* FindSchemaClass(const std::string& name)
{
    for (size_t i = 0; i < sizeof(kSchemaClasses) / sizeof(kSchemaClasses[0]); i++)
        if (name == kSchemaClasses[i].name)
            return &kSchemaClasses[i];
    return 0;
}

class Walker
{
public:
    Walker(unsigned char* d, size_t n, const std::vector<CustomClass>& custom)
        : m_d(d), m_size(n), m_pos(0), m_end(0), m_ok(true)
    {
        for (size_t i = 0; i < custom.size(); i++)
            m_custom[custom[i].name] = &custom[i];
        m_stats.objects = m_stats.controlBlocks = m_stats.timelinePaths = 0;
    }

    bool Run(std::string& error)
    {
        if (!Header())
        {
            error = m_error;
            return false;
        }
        Node* root = Object();
        delete root;
        if (m_ok && m_pos != m_end)
            Fail("DATA not fully read");
        if (!m_ok)
        {
            error = m_error;
            // Undo: put back every byte changed so far.
            for (size_t i = m_undo.size(); i-- > 0;)
                m_d[m_undo[i].first] = m_undo[i].second;
            return false;
        }
        return true;
    }

    UpgradeStats m_stats;

private:
    unsigned char* m_d;
    size_t m_size, m_pos, m_end;
    bool m_ok;
    std::string m_error;
    std::vector<std::string> m_strings;
    std::map<std::string, const CustomClass*> m_custom;
    std::vector<std::pair<size_t, unsigned char> > m_undo;

    void Fail(const std::string& why)
    {
        if (m_ok)
        {
            char at[32];
            sprintf(at, " at 0x%X", (unsigned)m_pos);
            m_error = why + at;
        }
        m_ok = false;
    }

    bool Need(size_t n)
    {
        if (!m_ok || m_pos + n > m_end)
        {
            Fail("past the end of DATA");
            return false;
        }
        return true;
    }

    unsigned U8() { if (!Need(1)) return 0; return m_d[m_pos++]; }
    unsigned U16() { if (!Need(2)) return 0; unsigned v = (m_d[m_pos] << 8) | m_d[m_pos + 1]; m_pos += 2; return v; }
    unsigned U32()
    {
        if (!Need(4)) return 0;
        unsigned v = ((unsigned)m_d[m_pos] << 24) | (m_d[m_pos + 1] << 16) | (m_d[m_pos + 2] << 8) | m_d[m_pos + 3];
        m_pos += 4;
        return v;
    }

    void Put(size_t at, unsigned char v)
    {
        if (m_d[at] != v)
        {
            m_undo.push_back(std::make_pair(at, m_d[at]));
            m_d[at] = v;
        }
    }

    static unsigned Read32(const unsigned char* p)
    {
        return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    }

    bool Header()
    {
        if (m_size < 20 || memcmp(m_d, "XUIB", 4) != 0)
        {
            m_error = "not a XUIB scene";
            return false;
        }
        unsigned flags = Read32(m_d + 8);
        unsigned sections = (m_d[18] << 8) | m_d[19];
        size_t p = 20 + ((flags & 1) ? 40 : 0);
        size_t strnAt = 0, strnSize = 0, dataAt = 0, dataSize = 0;
        for (unsigned i = 0; i < sections; i++, p += 12)
        {
            if (p + 12 > m_size)
            {
                m_error = "section table past the end";
                return false;
            }
            unsigned at = Read32(m_d + p + 4), size = Read32(m_d + p + 8);
            if (!memcmp(m_d + p, "STRN", 4)) { strnAt = at; strnSize = size; }
            if (!memcmp(m_d + p, "DATA", 4)) { dataAt = at; dataSize = size; }
        }
        if (!dataAt || dataAt + dataSize > m_size || strnAt + strnSize > m_size)
        {
            m_error = "no DATA section, or a section past the end";
            return false;
        }
        for (size_t q = strnAt; q + 2 <= strnAt + strnSize;)
        {
            unsigned n = (m_d[q] << 8) | m_d[q + 1];
            q += 2;
            std::string s;
            for (unsigned i = 0; i < n && q + 2 <= strnAt + strnSize; i++, q += 2)
            {
                unsigned c = (m_d[q] << 8) | m_d[q + 1];
                s += c < 0x80 ? (char)c : '?';
            }
            m_strings.push_back(s);
        }
        m_pos = dataAt;
        m_end = dataAt + dataSize;
        return true;
    }

    std::string Str(unsigned index) const
    {
        return index > 0 && index <= m_strings.size() ? m_strings[index - 1] : std::string();
    }

    // The class's levels from XuiElement down to it (custom classes may stack).
    bool Levels(const std::string& cls, std::vector<Level>& out)
    {
        std::vector<const CustomClass*> custom;
        std::string name = cls;
        while (!FindSchemaClass(name))
        {
            std::map<std::string, const CustomClass*>::const_iterator c = m_custom.find(name);
            if (c == m_custom.end())
            {
                Fail("unknown class " + cls);
                return false;
            }
            custom.push_back(c->second);
            name = c->second->base;
        }
        std::vector<const SchemaClass*> chain;
        for (const SchemaClass* s = FindSchemaClass(name); s; s = *s->base ? FindSchemaClass(s->base) : 0)
            chain.push_back(s);
        out.clear();
        for (size_t i = chain.size(); i-- > 0;)
        {
            Level l;
            l.cls = chain[i]->name;
            l.control = l.cls == "XuiControl";
            for (int p = 0; p < chain[i]->count; p++)
            {
                const SchemaProp* sp = &kSchemaProps[chain[i]->first + p];
                // 1888's XuiControl: no NavTabForward / NavTabBackward.
                if (l.control && (!strcmp(sp->name, "NavTabForward") || !strcmp(sp->name, "NavTabBackward")))
                    continue;
                l.props.push_back(sp);
            }
            out.push_back(l);
        }
        for (size_t i = custom.size(); i-- > 0;)
        {
            Level l;
            l.cls = custom[i]->name;
            l.control = false;
            l.types = custom[i]->types;
            out.push_back(l);
        }
        return true;
    }

    void Scalar(int type)
    {
        if (type == kString) U16();
        else if (type == kBool) U8();
        else U32();
    }

    void Value(const SchemaProp* p, int type)
    {
        if (type == kObject)
        {
            if (U16() && p)
            {
                Level sub;
                sub.control = false;
                for (int i = 0; i < p->subCount; i++)
                    sub.props.push_back(&kSchemaProps[p->sub + i]);
                Block(sub, 0);
            }
            return;
        }
        if (p && (p->flags & 1))        // indexed: a count, then that many values
        {
            unsigned n = U8();
            for (unsigned i = 0; i < n && m_ok; i++)
                Scalar(type);
            return;
        }
        Scalar(type);
    }

    // One level's property block. For 1888's XuiControl, bits 8.. move up by two.
    void Block(const Level& level, unsigned* idOut)
    {
        unsigned x = U8();
        if (!x || !m_ok)
            return;
        unsigned maskLen = x & 7, count = (x >> 3) + 1;
        if (!Need(maskLen))
            return;
        if (maskLen > 4)
        {
            Fail("property mask longer than 32 bits");
            return;
        }
        size_t maskAt = m_pos;
        unsigned mask = 0;
        for (unsigned i = 0; i < maskLen; i++)
            mask = (mask << 8) | m_d[m_pos++];
        unsigned seen = 0;
        for (unsigned bit = 0; bit < maskLen * 8 && m_ok; bit++)
        {
            if (!(mask >> bit & 1))
                continue;
            seen++;
            if (bit >= level.Count())
            {
                Fail("property bit beyond " + level.cls + "'s list");
                return;
            }
            const SchemaProp* p = level.props.empty() ? 0 : level.props[bit];
            if (idOut && bit == 0 && level.cls == "XuiElement")
            {
                size_t at = m_pos;
                *idOut = Need(2) ? (m_d[at] << 8 | m_d[at + 1]) : 0;
            }
            Value(p, level.Type(bit));
        }
        if (seen != count)
        {
            Fail("property mask doesn't match its count");
            return;
        }
        if (level.control && (mask & ~0xFFu))
        {
            unsigned upgraded = (mask & 0xFF) | ((mask & ~0xFFu) << 2);
            if (maskLen < 4 && (upgraded >> (maskLen * 8)))
            {
                Fail("XuiControl mask doesn't fit after renumbering");
                return;
            }
            for (unsigned i = 0; i < maskLen; i++)
                Put(maskAt + i, (unsigned char)(upgraded >> (8 * (maskLen - 1 - i))));
            m_stats.controlBlocks++;
        }
    }

    Node* Object()
    {
        Node* node = new Node;
        node->id = 0;
        unsigned ci = U16();
        node->cls = Str(ci);
        unsigned flags = U8();
        if (!m_ok)
            return node;
        if (!ci || ci > m_strings.size() || flags > 7)
        {
            Fail("not an object");
            return node;
        }
        m_stats.objects++;
        if (flags & 1)
        {
            U16();                      // total property count (all levels)
            std::vector<Level> levels;
            if (!Levels(node->cls, levels))
                return node;
            for (size_t i = 0; i < levels.size() && m_ok; i++)
                Block(levels[i], &node->id);
        }
        if (flags & 2)
        {
            unsigned n = U32();
            for (unsigned i = 0; i < n && m_ok; i++)
                node->children.push_back(Object());
        }
        if ((flags & 4) && m_ok)
            Timelines(node, (flags & 2) != 0);
        return node;
    }

    // A timeline's target by Id: direct children first, then deeper (first match wins).
    const Node* FindTarget(const Node* node, unsigned id) const
    {
        std::vector<const Node*> level(node->children.begin(), node->children.end());
        while (!level.empty())
        {
            std::vector<const Node*> next;
            for (size_t i = 0; i < level.size(); i++)
            {
                if (level[i]->id && Str(level[i]->id) == Str(id))
                    return level[i];
                next.insert(next.end(), level[i]->children.begin(), level[i]->children.end());
            }
            level.swap(next);
        }
        return 0;
    }

    void Timelines(const Node* node, bool hasChildren)
    {
        unsigned frames = U32();
        for (unsigned i = 0; i < frames && m_ok; i++)
        {
            U16();                      // name
            U32();                      // time
            U8();                       // command
            U16();                      // target
        }
        unsigned timelines = hasChildren ? U32() : 0;
        for (unsigned t = 0; t < timelines && m_ok; t++)
        {
            unsigned target = U16();
            const Node* el = target ? FindTarget(node, target) : node;
            if (!el)
            {
                Fail("timeline target " + Str(target) + " not found");
                return;
            }
            std::vector<Level> levels;          // base first; a path's level 0 is the element's own class
            if (!Levels(el->cls, levels))
                return;
            unsigned paths = U32();
            std::vector<int> types;
            for (unsigned i = 0; i < paths && m_ok; i++)
            {
                unsigned n = U8(), levelIndex = U8();
                size_t indexAt = m_pos;
                unsigned index = U8();
                if (!m_ok)
                    return;
                if (levelIndex >= levels.size())
                {
                    Fail("timeline property level out of range");
                    return;
                }
                const Level& level = levels[levels.size() - 1 - levelIndex];
                if (index >= level.Count())
                {
                    Fail("timeline property out of range on " + level.cls);
                    return;
                }
                const SchemaProp* p = level.props.empty() ? 0 : level.props[index];
                int type = level.Type(index);
                for (unsigned s = 1; s < (n & 0x7F) && m_ok; s++)
                {
                    unsigned j = U8();
                    if (!p || (int)j >= p->subCount)
                    {
                        Fail("timeline sub-property out of range");
                        return;
                    }
                    p = &kSchemaProps[p->sub + j];
                    type = p->type;
                }
                if (n & 0x80)
                    U32();                      // array index
                if (level.control && index >= 8)
                {
                    Put(indexAt, (unsigned char)(index + 2));
                    m_stats.timelinePaths++;
                }
                types.push_back(type);
            }
            unsigned keys = U32();
            for (unsigned k = 0; k < keys && m_ok; k++)
            {
                U32();                          // time
                U8();                           // interpolation
                U8(); U8(); U8();               // ease
                for (size_t v = 0; v < types.size() && m_ok; v++)
                    Scalar(types[v]);
            }
        }
    }
};

}  // namespace

bool IsScene(const unsigned char* data, size_t size)
{
    return size >= 4 && !memcmp(data, "XUIB", 4);
}

bool Upgrade1888(unsigned char* data, size_t size, const std::vector<CustomClass>& custom, UpgradeStats* stats,
                 std::string& error)
{
    Walker w(data, size, custom);
    bool ok = w.Run(error);
    if (stats)
        *stats = w.m_stats;
    return ok;
}

}  // namespace xur
