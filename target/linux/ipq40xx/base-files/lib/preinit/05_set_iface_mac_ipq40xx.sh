. /lib/functions.sh

ipq40xx_mikrotik_mac_address() {
	base_mac=$(cat /sys/firmware/mikrotik/hard_config/mac_base)
	counter=0
	if [ ! -d /sys/class/net/eth0/dsa ]; then
		return
	fi
	ip link set dev eth0 address $(macaddr_add "$base_mac" "$counter")
	# first port is WAN, and has mac_base MAC address
	if [ -d /sys/class/net/eth0/upper_wan ]; then
		iface=wan
		ip link set dev "$iface" address $(macaddr_add "$base_mac" "$counter")
		counter=$((counter+1))
	fi
	# each other port has a consecutive MAC address
	for member in /sys/class/net/eth0/upper*; do
		iface=${member#*_}
		case "$iface" in
		lan*|ether*)
			ip link set dev "$iface" address $(macaddr_add "$base_mac" "$counter")
			counter=$((counter+1))
			;;
		esac
	done
}


preinit_set_mac_address() {
	case $(board_name) in
	asus,map-ac2200)
		base_mac=$(mtd_get_mac_binary_ubi Factory 0x1006)
		ip link set dev eth0 address $(macaddr_add "$base_mac" 1)
		ip link set dev eth1 address $(macaddr_add "$base_mac" 3)
		;;
	asus,rt-ac42u)
		ip link set dev eth0 address $(mtd_get_mac_binary_ubi Factory 0x1006)
		ip link set dev eth1 address $(mtd_get_mac_binary_ubi Factory 0x9006)
		;;
	engenius,eap2200)
		base_mac=$(cat /sys/class/net/eth0/address)
		ip link set dev eth1 address $(macaddr_add "$base_mac" 1)
		;;
	extreme-networks,ws-ap3915i)
		ip link set dev eth0 address $(mtd_get_mac_ascii CFG1 ethaddr)
		;;
	linksys,ea8300|\
	linksys,mr8300)
		base_mac=$(mtd_get_mac_ascii devinfo hw_mac_addr)
		ip link set dev lan1 address $(macaddr_add "$base_mac" 1)
		ip link set dev eth0 address $(macaddr_setbit "$base_mac" 7)
		;;
	mikrotik,*)
		ipq40xx_mikrotik_mac_address
		;;
	zyxel,nbg6617)
		base_mac=$(cat /sys/class/net/eth0/address)
		ip link set dev eth0 address $(macaddr_add "$base_mac" 2)
		ip link set dev eth1 address $(macaddr_add "$base_mac" 3)
		;;
	esac
}

boot_hook_add preinit_main preinit_set_mac_address
