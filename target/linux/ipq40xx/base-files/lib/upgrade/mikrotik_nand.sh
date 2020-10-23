mikrotik_check_nand_blocksize() {
	local tar_file="$1"

	local ubi_kernel_part="$(find_mtd_index "$CI_KERNPART")"
	if [ ! "$ubi_kernel_part" ]; then
		echo "cannot find kernel ubi mtd partition"
		return 1
	fi

	local board_dir=$(tar tf "$tar_file" | grep -m 1 '^sysupgrade-.*/$')
	board_dir=${board_dir%/}
	local board_variant=${board_dir##*-}
	board_variant=${board_variant%k*}

	local nand_erasesize=$(cat \
		"/sys/class/mtd/mtd$ubi_kernel_part/erasesize")
	local sysupgrade_erasesize="$(( board_variant * 2**10 ))"
	if [ "$nand_erasesize" -ne  "$sysupgrade_erasesize" ]; then
		echo "device NAND has wrong erasesize ($((nand_erasesize / 2**10))k) for this sysupgrade image (${board_variant}k)"
		return 1
	fi
}

mikrotik_rm_nand_routeros() {
	local mtdnum="$( find_mtd_index "$CI_UBIPART" )"
	if [ ! "$mtdnum" ]; then
		echo "cannot find ubi mtd partition $CI_UBIPART"
		return 1
	fi

	local ubidev="$( nand_find_ubi "$CI_UBIPART" )"
	if [ ! "$ubidev" ]; then
		echo "active ubi partition not found, attach"
		ubiattach -m "$mtdnum" || true
		sync
		ubidev="$( nand_find_ubi "$CI_UBIPART" )"
	fi

	if [ ! "$ubidev" ]; then
		echo "active ubi partition still not found, formatting"
		ubiformat "/dev/mtd$mtdnum" -y
		ubiattach -m "$mtdnum"
		sync
		ubidev="$( nand_find_ubi "$CI_UBIPART" )"
	else
		local ubi_OEM_vol="$(nand_find_volume "$ubidev" RouterOS)"
		if [ "$ubi_OEM_vol" ]; then
			echo "remove OEM firmware volume"
			ubirmvol "/dev/$ubidev" -N RouterOS &> /dev/null || \
				ubiformat "/dev/mtd$mtdnum" -y
		fi
	fi
}
