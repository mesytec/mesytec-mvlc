#include <mesytec-mvlc/mesytec-mvlc.h>
#include <argh.h>

// Sends super transactions consisting of N reference words for a range of N
// and reports which request/response sizes fail. Used to find USB transfer
// size dependent problems.

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    argh::parser parser;
    parser.add_params({"--mvlc", "--log-level", "--first", "--last", "--step", "--repeat"});
    parser.parse(argc, argv);

    std::string url;
    std::string logLevelName("warn");
    size_t first = 1, last = 600, step = 1, repeat = 1;

    parser("--mvlc") >> url;
    parser("--log-level") >> logLevelName;
    parser("--first") >> first;
    parser("--last") >> last;
    parser("--step") >> step;
    parser("--repeat") >> repeat;

    if (auto logLevel = log_level_from_string(logLevelName))
        spdlog::set_level(*logLevel);

    auto mvlc = make_mvlc(url);

    if (auto ec = mvlc.connect())
    {
        fmt::print(stderr, "Error connecting to MVLC: {}\n", ec.message());
        return 1;
    }

    fmt::print("connected: {}\n", mvlc.connectionInfo());

    std::vector<u32> response;
    size_t failures = 0;

    for (size_t n = first; n <= last; n += step)
    {
        for (size_t r = 0; r < repeat; ++r)
        {
            SuperCommandBuilder sb;
            for (size_t i = 0; i < n; ++i)
                sb.addReferenceWord(static_cast<u16>(i));

            const auto requestBytes = make_command_buffer(sb).size() * sizeof(u32);
            response.clear();
            auto tStart = std::chrono::steady_clock::now();
            auto ec = mvlc.superTransaction(sb, response);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tStart);

            if (ec)
            {
                ++failures;
                fmt::print("FAIL n={}, request={} bytes, ec={}, {} ms\n",
                           n, requestBytes, ec.message(), elapsed.count());
            }
            else if (elapsed.count() > 100)
            {
                fmt::print("SLOW n={}, request={} bytes, response={} bytes, {} ms\n",
                           n, requestBytes, response.size() * sizeof(u32), elapsed.count());
            }
        }
    }

    fmt::print("done, {} failures\n", failures);

    return failures ? 1 : 0;
}
