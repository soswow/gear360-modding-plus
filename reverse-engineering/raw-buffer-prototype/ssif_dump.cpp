#include <algorithm>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

struct LinuxDumpDescriptor {
    char filename[64];
    std::uint32_t physical_buffer;
    std::uint32_t byte_count;
    std::uint32_t suffix_flag;
};
static_assert(sizeof(LinuxDumpDescriptor) == 0x4c, "descriptor ABI size");
static_assert(offsetof(LinuxDumpDescriptor, filename) == 0x00, "filename offset");
static_assert(offsetof(LinuxDumpDescriptor, physical_buffer) == 0x40, "physical buffer offset");
static_assert(offsetof(LinuxDumpDescriptor, byte_count) == 0x44, "byte count offset");
static_assert(offsetof(LinuxDumpDescriptor, suffix_flag) == 0x48, "suffix flag offset");

struct Metadata {
    std::uint32_t pixel_bits;
    std::uint32_t memory_packing;
};

struct SplitRows {
    std::vector<std::uint8_t> front;
    std::vector<std::uint8_t> rear;
};

static std::size_t checked_size(std::uint32_t stride, std::uint32_t height) {
    if (stride == 0 || (stride & 1U) != 0)
        throw std::invalid_argument("stride must be positive and even");
    if (height == 0)
        throw std::invalid_argument("height must be positive");
    const std::size_t s = stride;
    const std::size_t h = height;
    if (h > std::numeric_limits<std::size_t>::max() / s)
        throw std::overflow_error("stride * height overflows size_t");
    return s * h;
}

static SplitRows split_ssif_rows(const std::vector<std::uint8_t>& source,
                                 std::uint32_t stride,
                                 std::uint32_t height,
                                 const Metadata& /*metadata*/) {
    const std::size_t required = checked_size(stride, height);
    if (required > source.size())
        throw std::invalid_argument("source buffer is shorter than stride * height");
    const std::size_t half = stride / 2U;
    SplitRows out;
    out.front.reserve(half * static_cast<std::size_t>(height));
    out.rear.reserve(half * static_cast<std::size_t>(height));
    for (std::size_t row = 0; row < height; ++row) {
        const auto begin = source.begin() + row * stride;
        out.front.insert(out.front.end(), begin, begin + half);
        out.rear.insert(out.rear.end(), begin + half, begin + stride);
    }
    return out;
}

static std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open input: " + path);
    const auto end = in.tellg();
    if (end < 0) throw std::runtime_error("cannot size input: " + path);
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("input file is too large for this host");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("cannot read input: " + path);
    return data;
}

static void write_file(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create output: " + path);
    if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("cannot write output: " + path);
}

static std::uint32_t parse_u32(const char* arg, const char* label) {
    std::size_t used = 0;
    const std::string value(arg);
    const auto parsed = std::stoull(value, &used, 0);
    if (used != value.size() || parsed > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument(std::string("invalid ") + label);
    return static_cast<std::uint32_t>(parsed);
}

static std::uint32_t read_le_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

static void inspect_descriptor(const std::string& path) {
    const auto bytes = read_file(path);
    if (bytes.size() != sizeof(LinuxDumpDescriptor))
        throw std::invalid_argument("descriptor file must be exactly 0x4c bytes");
    const auto nul = std::find(bytes.begin(), bytes.begin() + 64, 0);
    const std::string filename(bytes.begin(), nul);
    std::cout << "filename=" << filename << '\n'
              << "physical_buffer=0x" << std::hex << read_le_u32(bytes, 0x40) << std::dec << '\n'
              << "byte_count=" << read_le_u32(bytes, 0x44) << '\n'
              << "suffix_flag=" << read_le_u32(bytes, 0x48) << '\n';
}

static void print_image_word(const char* label, const std::vector<std::uint8_t>& bytes,
                             std::size_t offset) {
    std::cout << label << "=0x" << std::hex << std::setw(8) << std::setfill('0')
              << read_le_u32(bytes, offset) << std::dec << std::setfill(' ') << '\n';
}

static void inspect_image(const std::string& path) {
    constexpr std::size_t image_descriptor_size = 0x88;
    const auto bytes = read_file(path);
    if (bytes.size() != image_descriptor_size)
        throw std::invalid_argument("stCAPTURE_IMAGE file must be exactly 0x88 bytes");

    print_image_word("geometry_0x00", bytes, 0x00);
    print_image_word("geometry_0x04", bytes, 0x04);
    print_image_word("geometry_0x08", bytes, 0x08);
    print_image_word("geometry_0x0c", bytes, 0x0c);
    print_image_word("stride_0x20", bytes, 0x20);
    print_image_word("format_0x24", bytes, 0x24);
    const std::uint32_t packtype = read_le_u32(bytes, 0x28);
    print_image_word("packtype_0x28", bytes, 0x28);
    print_image_word("pixelenum_0x2c", bytes, 0x2c);
    print_image_word("memorypacking_0x30", bytes, 0x30);
    print_image_word("address_type_0x40", bytes, 0x40);
    print_image_word("physical_plane0_0x44", bytes, 0x44);
    print_image_word("virtual_plane0_0x54", bytes, 0x54);
    print_image_word("bytecount_0x74", bytes, 0x74);
    std::cout << "libmm_photo_packtype_guard=" << (packtype == 3U ? "pass" : "fail")
              << " (packtype == 3; generic consumer guard only, not image validity or capture success)\n";
}

static void self_check() {
    const std::vector<std::uint8_t> fixture{
        0x10, 0x11, 0x12, 0x13, 0x20, 0x21, 0x22, 0x23,
        0x30, 0x31, 0x32, 0x33, 0x40, 0x41, 0x42, 0x43};
    const Metadata meta{10, 16};
    const auto split = split_ssif_rows(fixture, 8, 2, meta);
    const std::vector<std::uint8_t> expected_front{0x10,0x11,0x12,0x13,0x30,0x31,0x32,0x33};
    const std::vector<std::uint8_t> expected_rear{0x20,0x21,0x22,0x23,0x40,0x41,0x42,0x43};
    if (split.front != expected_front || split.rear != expected_rear)
        throw std::runtime_error("synthetic row-order fixture failed");
    auto rejects = [&](const std::vector<std::uint8_t>& src, std::uint32_t stride,
                       std::uint32_t height) {
        try { (void)split_ssif_rows(src, stride, height, meta); }
        catch (const std::exception&) { return true; }
        return false;
    };
    if (!rejects(fixture, 7, 2)) throw std::runtime_error("odd stride was accepted");
    if (!rejects(std::vector<std::uint8_t>(15), 8, 2)) throw std::runtime_error("truncated source was accepted");
    if (!rejects(fixture, std::numeric_limits<std::uint32_t>::max() - 1U,
                 std::numeric_limits<std::uint32_t>::max()))
        throw std::runtime_error("overflow/oversized request was accepted");
    std::cout << "self-check passed: front/rear row order, odd stride, truncated source, overflow/size rejection\n";
}

static void usage(const char* exe) {
    std::cerr << "Usage:\n  " << exe << " inspect <saved-descriptor.bin>\n  "
              << "  " << exe << " inspect-image <saved-stCAPTURE_IMAGE.bin>\n  "
              << exe << " split <saved-buffer.bin> <stride> <height> <output-prefix> [pixel-bits memory-packing]\n  "
              << exe << " --self-check\n";
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-check") {
            self_check();
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "inspect") {
            inspect_descriptor(argv[2]);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "inspect-image") {
            inspect_image(argv[2]);
            return 0;
        }
        if ((argc == 6 || argc == 8) && std::string(argv[1]) == "split") {
            const auto bytes = read_file(argv[2]);
            const auto stride = parse_u32(argv[3], "stride");
            const auto height = parse_u32(argv[4], "height");
            const Metadata metadata{argc == 8 ? parse_u32(argv[6], "pixel bits") : 0,
                                    argc == 8 ? parse_u32(argv[7], "memory packing") : 0};
            const auto split = split_ssif_rows(bytes, stride, height, metadata);
            const std::string prefix(argv[5]);
            write_file(prefix + ".front.raw", split.front);
            write_file(prefix + ".rear.raw", split.rear);
            std::cout << "wrote " << split.front.size() << " bytes per stream\n";
            return 0;
        }
        usage(argv[0]);
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
