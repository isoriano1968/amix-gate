# verify-stock.sh -- sourced; refuses to patch a kernel this port has never been built against.
#
#   . "$HERE/tools/config-load.sh"
#   . "$HERE/tools/verify-stock.sh"
#   verify_stock "$STOCK"
#
# WHY THIS IS A HARD CHECK AND NOT A WARNING
#
# This port is a patch layer, and the patch scripts address the kernel by HARD-CODED byte
# offsets: `fpu_save` at 0x132, `hardbus` at 0x5b3c2, relocation r_offsets, byte-pattern
# assertions.  Against a different build of /stand/unix those offsets do not point at nothing --
# they point at *something else*.  The build would succeed and produce a kernel patched at the
# wrong bytes.
#
# So the failure mode this guards against is not "the build breaks"; it is "the build works and
# the result is quietly wrong", which is the worst kind and the one this project has spent the
# most effort learning to refuse.
#
# The reference is the /stand/unix of AMIX SVR4 2.1c as distributed.  Whether every 2.1c
# installation carries this exact image is NOT established -- so a mismatch asks for a report
# rather than assuming the user is wrong.
#
# One difference IS established: the AMIX installation writes the size of the UNIX_Swap
# partition into swapfile.nswap (.data swapfile+0x98, 4 bytes).  The public 2.1c
# kernels/2.1c/unix (61440 blocks) and a 2.1p2a installation (409600) differ there and nowhere
# else.  So an image whose sha256 with those 4 bytes set to 0 (tools/kernel-nswap.py) is
# AMIX_STOCK_CODE_SHA256 is the same code and is accepted; its nswap stays as it was.

# AMIX SVR4 2.1c /stand/unix.  Every acceptance run in this project was built from it.
AMIX_STOCK_SHA256=7d26cb6f04991be5776d9e5361259b20b413d97e3da33bf88e6f312e7be2ec23
# The same image with swapfile.nswap set to 0: every 2.1c installation's /stand/unix.
AMIX_STOCK_CODE_SHA256=fa124e6503eb13131fb470057d355c45ee91ca167865956b1f9e9f10b6bad523

verify_stock() {
	_vs_file="$1"

	if [ ! -f "$_vs_file" ]; then
		echo "[FAIL] the AMIX kernel to patch was not found:"
		echo "         $_vs_file"
		echo "       Set AMIX_ROOT in config.sh to your own mounted AMIX installation."
		exit 1
	fi

	_vs_got=$(sha256sum "$_vs_file" | cut -d' ' -f1)
	[ "$_vs_got" = "$AMIX_STOCK_SHA256" ] && { echo "[*] stock kernel verified: $_vs_file"; return 0; }
	_vs_code=$(python3 "$(cd "$(dirname "$0")" && pwd)/tools/kernel-nswap.py" "$_vs_file" 2>/dev/null) || _vs_code=
	if [ "${_vs_code%% *}" = "$AMIX_STOCK_CODE_SHA256" ]; then
		echo "[*] stock kernel verified: $_vs_file (2.1c code, swapfile.nswap ${_vs_code#* } blocks)"
		return 0
	fi

	if [ "${AMIX_ALLOW_UNKNOWN_STOCK:-0}" = "1" ]; then
		echo "[!] ==================================================================="
		echo "[!] UNKNOWN STOCK KERNEL, and you have set AMIX_ALLOW_UNKNOWN_STOCK=1."
		echo "[!]   file     $_vs_file"
		echo "[!]   sha256   $_vs_got"
		echo "[!]   expected $AMIX_STOCK_SHA256"
		echo "[!] The patch scripts use hard-coded byte offsets.  If this image differs"
		echo "[!] from the reference anywhere in .text, they will patch the WRONG BYTES"
		echo "[!] and the resulting kernel may be quietly incorrect.  Do not trust any"
		echo "[!] measurement from it until the offsets have been re-established."
		echo "[!] ==================================================================="
		return 0
	fi

	echo "[FAIL] this is not the AMIX kernel this port is built against."
	echo
	echo "         file     $_vs_file"
	echo "         sha256   $_vs_got"
	echo "         expected $AMIX_STOCK_SHA256   (AMIX SVR4 2.1c /stand/unix)"
	echo
	echo "       The patch scripts address the kernel by hard-coded byte offsets.  Against a"
	echo "       different build they would patch the wrong bytes and still report success,"
	echo "       so the build stops here rather than producing a kernel nobody can trust."
	echo
	echo "       If your AMIX 2.1c genuinely carries a different /stand/unix, that is worth"
	echo "       knowing and worth reporting -- please open an issue with the hash above."
	echo "       To proceed anyway, at your own risk:"
	echo "           AMIX_ALLOW_UNKNOWN_STOCK=1 sh $(basename "$0")"
	exit 1
}
