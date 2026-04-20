#!/usr/bin/env atf-sh

atf_test_case round_trip
round_trip_head() {
	atf_set "descr" "Compressing and extracting a one-block ISO round-trips cleanly"
}
round_trip_body() {
	srcdir="$(atf_get_srcdir)"
	prog="${srcdir}/../ciso-maker"

	truncate -s 2048 tiny.iso

	atf_check -s exit:0 -o ignore -e ignore "${prog}" tiny.iso tiny.cso
	atf_check -s exit:0 -o ignore -e ignore "${prog}" -x tiny.cso tiny.out.iso
	atf_check -s exit:0 -o empty -e empty cmp -s tiny.iso tiny.out.iso
}

atf_test_case multi_block_round_trip
multi_block_round_trip_head() {
	atf_set "descr" "Compressing and extracting a multi-block ISO round-trips cleanly"
}
multi_block_round_trip_body() {
	srcdir="$(atf_get_srcdir)"
	prog="${srcdir}/../ciso-maker"

	dd if=/dev/zero of=multi.iso bs=2048 count=3 >/dev/null 2>&1
	printf 'block-two-marker' | dd of=multi.iso bs=1 seek=2304 conv=notrunc >/dev/null 2>&1
	printf 'block-three-marker' | dd of=multi.iso bs=1 seek=4352 conv=notrunc >/dev/null 2>&1

	atf_check -s exit:0 -o ignore -e ignore "${prog}" multi.iso multi.cso
	atf_check -s exit:0 -o ignore -e ignore "${prog}" -x multi.cso multi.out.iso
	atf_check -s exit:0 -o empty -e empty cmp -s multi.iso multi.out.iso
}

atf_test_case reject_same_input_output
reject_same_input_output_head() {
	atf_set "descr" "The tool rejects in-place conversion to avoid truncating the input"
}
reject_same_input_output_body() {
	srcdir="$(atf_get_srcdir)"
	prog="${srcdir}/../ciso-maker"

	truncate -s 2048 same.iso

	atf_check \
		-s exit:1 \
		-o ignore \
		-e match:"Input and output must be different files" \
		"${prog}" same.iso same.iso
}

atf_test_case reject_truncated_cso
reject_truncated_cso_head() {
	atf_set "descr" "Truncated CSO input is rejected cleanly"
}
reject_truncated_cso_body() {
	srcdir="$(atf_get_srcdir)"
	prog="${srcdir}/../ciso-maker"

	truncate -s 2048 tiny.iso
	atf_check -s exit:0 -o ignore -e ignore "${prog}" tiny.iso tiny.cso
	size="$(wc -c < tiny.cso)"
	dd if=tiny.cso of=tiny.truncated.cso bs=1 count=$((size - 1)) >/dev/null 2>&1

	atf_check \
		-s exit:1 \
		-o ignore \
		-e match:"read error|file read error" \
		"${prog}" -x tiny.truncated.cso tiny.out.iso
}

atf_test_case reject_invalid_cso
reject_invalid_cso_head() {
	atf_set "descr" "Malformed CSO input is rejected cleanly"
}
reject_invalid_cso_body() {
	srcdir="$(atf_get_srcdir)"
	prog="${srcdir}/../ciso-maker"

	printf 'not-a-cso\n' > bad.cso

	atf_check \
		-s exit:1 \
		-o ignore \
		-e match:"file read error|ciso file format error" \
		"${prog}" -x bad.cso bad.iso
}

atf_test_case plain_block_cso
plain_block_cso_head() {
	atf_set "descr" "Incompressible input is stored as a plain CSO block"
}
plain_block_cso_body() {
	srcdir="$(atf_get_srcdir)"
	prog="${srcdir}/../ciso-maker"

	dd if=/dev/urandom of=plain.iso bs=2048 count=1 >/dev/null 2>&1

	atf_check -s exit:0 -o ignore -e ignore "${prog}" plain.iso plain.cso
	atf_check -s exit:0 -o ignore -e ignore "${prog}" -x plain.cso plain.out.iso
	atf_check -s exit:0 -o empty -e empty cmp -s plain.iso plain.out.iso

	msb="$(od -An -t u1 -j 27 -N 1 plain.cso | awk '{print $1}')"
	atf_check_equal "128" "${msb}"
}

atf_init_test_cases() {
	atf_add_test_case round_trip
	atf_add_test_case multi_block_round_trip
	atf_add_test_case reject_same_input_output
	atf_add_test_case reject_truncated_cso
	atf_add_test_case reject_invalid_cso
	atf_add_test_case plain_block_cso
}
