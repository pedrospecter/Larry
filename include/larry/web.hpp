#pragma once

#include "larry/constellation.hpp"
#include "larry/electron.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace larry {

/// A hit of a search: a title and a snippet, with where the page is.
struct Hit {
    std::string title;
    std::string snippet;
    std::string url;
};

/// A meaning of a word, from Wiktionary: one of Larry's categories and the
/// first definitions given for it.
struct Meaning {
    Bytes category;
    std::vector<std::string> definitions;
};

/// A page kept on the machine as plain text (W1).
struct Page {
    std::string title;
    std::string source;  ///< The URL it came from.
    std::string text;
    std::filesystem::path file;  ///< Where it is kept, under content/<locale>/.
};

/// The web as a source (W1): Wikipedia for searching and reading, Wiktionary
/// for the meanings of a word, any URL for its text. The transport is the
/// curl command on the machine (Q31), or a function the tests give with
/// recorded answers. Nothing fetched is a conception: it is content, kept on
/// the machine in content/<locale>/ with its source and date, to classify
/// (W2) and study (W4).
class Web {
public:
    /// A function from a URL to the body of the answer; throws on failure.
    using Transport = std::function<std::string(const std::string& url)>;

    /// With no transport, the curl command; with no directory, content/<locale>
    /// in the repository, or LARRY_CONTENT_DIR.
    explicit Web(Language language, Transport transport = {},
                 std::filesystem::path directory = {});

    /// What Larry says it is when it asks a server.
    [[nodiscard]] static std::string user_agent();

    /// Runs curl for a URL and gives the body. Throws std::runtime_error when
    /// curl is not there or the server answers with an error; a "too many
    /// requests" answer is tried once more after a pause.
    [[nodiscard]] static std::string curl(const std::string& url);

    /// Where the content of this language is kept.
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

    /// Wikipedia's search: the pages whose text matches the words, best first.
    [[nodiscard]] std::vector<Hit> search(std::string_view words, std::size_t limit = 5) const;

    /// The plain text of a Wikipedia article, by title, or of any web page,
    /// by URL, with its tags stripped.
    [[nodiscard]] std::string text_of(std::string_view title_or_url) const;

    /// Keeps a page under directory()/<name>.txt, with "# source:", "# fetched:"
    /// and "# title:" on the first lines, and gives it back.
    [[nodiscard]] Page fetch(std::string_view title_or_url) const;

    /// The English meanings of a word, from Wiktionary: the parts of speech it
    /// has, as Larry's categories, each with its first definitions.
    [[nodiscard]] std::vector<Meaning> define(std::string_view word) const;

    /// The page kept under this name, when it is there.
    [[nodiscard]] static std::optional<Page> read_page(const std::filesystem::path& file);

    // The parsers, on their own for the tests.
    [[nodiscard]] static std::vector<Hit> parse_search(std::string_view json);
    [[nodiscard]] static std::string parse_extract(std::string_view json);
    [[nodiscard]] static std::vector<Meaning> parse_wiktionary(std::string_view extract);
    [[nodiscard]] static std::string strip_html(std::string_view html);
    [[nodiscard]] static std::string slug(std::string_view title);
    [[nodiscard]] static std::string url_encode(std::string_view text);
    [[nodiscard]] static bool is_url(std::string_view text);

private:
    Language language_;
    Transport transport_;
    std::filesystem::path directory_;
};

}  // namespace larry
