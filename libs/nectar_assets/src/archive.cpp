#include "nectar_assets/archive.h"

#include "be_writer.h"

namespace nectar {

bool writeArchivePair(const ArchiveInput& in, ArchivePair& out, std::string& error)
{
	BeWriter w;
	w.u32(0); // cabecera: el juego la lee y la descarta
	w.u32(std::uint32_t(in.entries.size()));
	for (const ArchiveEntry& e : in.entries) {
		if (std::uint64_t(e.offset) + e.size > in.blob.size()) {
			error = "archive entry '" + e.name + "' runs past the end of the blob";
			return false;
		}
		w.u32(e.offset);
		w.u32(e.size);
		w.str(e.name);
	}
	out.dir = std::move(w.b);
	out.arc = in.blob;
	return true;
}

} // namespace nectar
