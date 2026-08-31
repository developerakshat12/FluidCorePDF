#include "search/WorkspaceSearchEngine.h"
#include "workspace/CardStackNode.h"
#include "workspace/ExcerptCardNode.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace FluidCore {

namespace {

std::string toLower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool containsCaseInsensitive(const std::string& haystack, const std::string& needle,
                             size_t* matchOffset = nullptr) {
    if (needle.empty()) {
        return false;
    }
    std::string hLower = toLower(haystack);
    std::string nLower = toLower(needle);
    size_t pos = hLower.find(nLower);
    if (pos != std::string::npos) {
        if (matchOffset) {
            *matchOffset = pos;
        }
        return true;
    }
    return false;
}

bool containsCaseSensitive(const std::string& haystack, const std::string& needle,
                            size_t* matchOffset = nullptr) {
    if (needle.empty()) {
        return false;
    }
    size_t pos = haystack.find(needle);
    if (pos != std::string::npos) {
        if (matchOffset) {
            *matchOffset = pos;
        }
        return true;
    }
    return false;
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void searchNode(const WorkspaceNode* node, const std::string& topLevelId,
                const WorkspaceModel& model, const std::string& cleanQuery,
                bool isTagQuery, const WorkspaceSearchOptions& options,
                std::vector<WorkspaceMatch>& outMatches) {
    if (!node) {
        return;
    }

    const auto containsMatch = [&](const std::string& haystack, size_t* outOffset) {
        if (options.caseSensitive) {
            return containsCaseSensitive(haystack, cleanQuery, outOffset);
        }
        return containsCaseInsensitive(haystack, cleanQuery, outOffset);
    };

    const Rectangle bounds = model.boundsOf(node->id());

    // 1. ExcerptCardNode search
    if (const auto* excerpt = dynamic_cast<const ExcerptCardNode*>(node)) {
        // Tag matching
        bool tagMatched = false;
        for (const auto& tag : excerpt->tags()) {
            size_t offset = 0;
            if (containsMatch(tag, &offset)) {
                WorkspaceMatch match;
                match.nodeId = excerpt->id();
                match.topLevelNodeId = topLevelId;
                match.title = "Excerpt (p. " + std::to_string(excerpt->sourcePageNo() + 1) + ")";
                match.snippet = "#" + tag;
                match.target = MatchTarget::Tag;
                match.bounds = bounds;
                match.matchOffset = offset;
                match.matchLength = cleanQuery.size();
                outMatches.push_back(std::move(match));
                tagMatched = true;
                break;
            }
        }

        // Inline hashtags in text snippet
        if (!tagMatched) {
            auto inlineTags = WorkspaceSearchEngine::extractHashtags(excerpt->textSnippet());
            for (const auto& tag : inlineTags) {
                size_t offset = 0;
                if (containsMatch(tag, &offset)) {
                    WorkspaceMatch match;
                    match.nodeId = excerpt->id();
                    match.topLevelNodeId = topLevelId;
                    match.title = "Excerpt (p. " + std::to_string(excerpt->sourcePageNo() + 1) + ")";
                    match.snippet = "#" + tag;
                    match.target = MatchTarget::Tag;
                    match.bounds = bounds;
                    match.matchOffset = offset;
                    match.matchLength = cleanQuery.size();
                    outMatches.push_back(std::move(match));
                    tagMatched = true;
                    break;
                }
            }
        }

        // If not a tag-only query or tag didn't match, check text snippet
        if (!options.tagsOnly && !isTagQuery && !tagMatched) {
            size_t offset = 0;
            if (containsMatch(excerpt->textSnippet(), &offset)) {
                WorkspaceMatch match;
                match.nodeId = excerpt->id();
                match.topLevelNodeId = topLevelId;
                match.title = "Excerpt (p. " + std::to_string(excerpt->sourcePageNo() + 1) + ")";
                match.snippet = excerpt->textSnippet();
                match.target = MatchTarget::TextSnippet;
                match.bounds = bounds;
                match.matchOffset = offset;
                match.matchLength = cleanQuery.size();
                outMatches.push_back(std::move(match));
            } else if (containsMatch(excerpt->id(), &offset)) {
                WorkspaceMatch match;
                match.nodeId = excerpt->id();
                match.topLevelNodeId = topLevelId;
                match.title = excerpt->id();
                match.snippet = excerpt->id();
                match.target = MatchTarget::NodeId;
                match.bounds = bounds;
                match.matchOffset = offset;
                match.matchLength = cleanQuery.size();
                outMatches.push_back(std::move(match));
            }
        }
    }
    // 2. CardStackNode search
    else if (const auto* stack = dynamic_cast<const CardStackNode*>(node)) {
        bool stackTagMatched = false;
        for (const auto& tag : stack->tags()) {
            size_t offset = 0;
            if (containsMatch(tag, &offset)) {
                WorkspaceMatch match;
                match.nodeId = stack->id();
                match.topLevelNodeId = topLevelId;
                match.title = stack->title();
                match.snippet = "#" + tag;
                match.target = MatchTarget::Tag;
                match.bounds = bounds;
                match.matchOffset = offset;
                match.matchLength = cleanQuery.size();
                outMatches.push_back(std::move(match));
                stackTagMatched = true;
                break;
            }
        }

        if (!options.tagsOnly && !isTagQuery && !stackTagMatched) {
            size_t offset = 0;
            if (containsMatch(stack->title(), &offset)) {
                WorkspaceMatch match;
                match.nodeId = stack->id();
                match.topLevelNodeId = topLevelId;
                match.title = stack->title();
                match.snippet = stack->title();
                match.target = MatchTarget::Title;
                match.bounds = bounds;
                match.matchOffset = offset;
                match.matchLength = cleanQuery.size();
                outMatches.push_back(std::move(match));
            }
        }

        // Recursively search children of the stack
        for (const auto& child : stack->children()) {
            if (child) {
                searchNode(child.get(), topLevelId, model, cleanQuery, isTagQuery, options,
                           outMatches);
            }
        }
    }
    // 3. Generic WorkspaceNode search
    else {
        if (!options.tagsOnly && !isTagQuery) {
            size_t offset = 0;
            if (containsMatch(node->id(), &offset)) {
                WorkspaceMatch match;
                match.nodeId = node->id();
                match.topLevelNodeId = topLevelId;
                match.title = node->id();
                match.snippet = node->id();
                match.target = MatchTarget::NodeId;
                match.bounds = bounds;
                match.matchOffset = offset;
                match.matchLength = cleanQuery.size();
                outMatches.push_back(std::move(match));
            }
        }
    }
}

} // namespace

std::vector<WorkspaceMatch> WorkspaceSearchEngine::search(const WorkspaceModel& model,
                                                          const std::string& query,
                                                          const WorkspaceSearchOptions& options) {
    std::string trimmed = trim(query);
    if (trimmed.empty()) {
        return {};
    }

    bool isTagQuery = false;
    std::string cleanQuery = trimmed;

    if (cleanQuery.rfind("tag:", 0) == 0 && cleanQuery.size() > 4) {
        isTagQuery = true;
        cleanQuery = trim(cleanQuery.substr(4));
    } else if (cleanQuery.rfind("#", 0) == 0 && cleanQuery.size() > 1) {
        isTagQuery = true;
        cleanQuery = cleanQuery.substr(1);
    }

    if (cleanQuery.empty()) {
        return {};
    }

    std::vector<WorkspaceMatch> matches;
    const auto allIds = model.allNodeIds();

    for (const auto& rootId : allIds) {
        const auto* node = model.find(rootId);
        if (node) {
            searchNode(node, rootId, model, cleanQuery, isTagQuery, options, matches);
        }
    }

    return matches;
}

std::vector<std::string> WorkspaceSearchEngine::extractHashtags(const std::string& text) {
    std::vector<std::string> tags;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '#') {
            size_t start = i + 1;
            size_t end = start;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) ||
                                         text[end] == '-' || text[end] == '_')) {
                ++end;
            }
            if (end > start) {
                std::string tag = text.substr(start, end - start);
                if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
                    tags.push_back(std::move(tag));
                }
            }
            i = end;
        } else {
            ++i;
        }
    }
    return tags;
}

std::vector<SearchResult>
WorkspaceSearchEngine::toSearchResults(const std::vector<WorkspaceMatch>& matches) {
    std::vector<SearchResult> results;
    results.reserve(matches.size());
    for (const auto& m : matches) {
        SearchResult res;
        res.entityId = m.nodeId;
        switch (m.target) {
            case MatchTarget::TextSnippet:
                res.entityType = "excerpt";
                break;
            case MatchTarget::Title:
                res.entityType = "stack";
                break;
            case MatchTarget::Tag:
                res.entityType = "tag";
                break;
            case MatchTarget::NodeId:
                res.entityType = "node";
                break;
        }
        res.pageIndex = -1;
        res.snippet = m.snippet;
        results.push_back(std::move(res));
    }
    return results;
}

} // namespace FluidCore
