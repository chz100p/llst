#ifndef LLST_VISUALIZATION_H_INCLUDED
#define LLST_VISUALIZATION_H_INCLUDED

#include <cstdio>
#include <iostream>
#include <fstream>

#include <map>
#include <analysis.h>

class ControlGraphVisualizer : public st::PlainNodeVisitor {
public:
    ControlGraphVisualizer(st::ControlGraph* graph, const std::string& fileName) : st::PlainNodeVisitor(graph) {
        m_stream.open(fileName.c_str(), std::ios::out | std::ios::trunc);

        m_stream << "digraph G2 {\n";
        firstDomain = true;
    }

    virtual ~ControlGraphVisualizer() { finish(); }

    virtual bool visitDomain(st::ControlDomain& domain);
    virtual bool visitNode(st::ControlNode& node);

private:
    void finish();
    bool isNodeProcessed(st::ControlNode* node);
    void markNode(st::ControlNode* node);

private:
    typedef std::map<st::ControlNode*, bool> TNodeMap;
    TNodeMap m_processedNodes;

    std::ofstream m_stream;
    bool firstDomain;
};

#endif
