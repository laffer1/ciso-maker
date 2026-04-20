#!/usr/bin/atf-sh

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

atf_init_test_cases() {
	atf_add_test_case round_trip
	atf_add_test_case reject_same_input_output
	atf_add_test_case reject_invalid_cso
}
