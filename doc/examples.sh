#!/bin/bash
# Generate examples from the tests

XMLPATCH="$PWD/../src/xmlpatch -s -p0"
OUTPUT="$PWD/examples.html"
QUOTE="sed -e s/\&/\&amp;/g -e s/</\&lt;/g -e s/>/\&gt;/g -e s/\t/\&#160;\&#160;/g"

function prepare () {
	cat > $OUTPUT <<-EOF
		<?xml version="1.0"?>
		<html>
			<head> 
				<title>XMLPatch examples</title> 
			</head> 
			<body> 
			</body> 
		</html>
	EOF

	$XMLPATCH -k <<-EOF
		<?xml version="1.0"?>
		<changes>
			<change file="$OUTPUT">
				<add sel="//head">
					<script type="text/javascript" src="data:text/javascript;base64,$(base64 highlight.js)"> </script>
					<style type="text/css">$(cat style.css)"</style>
				</add>
			</change>
		</changes>
	EOF
}

function add_dir () {
	cd $1
	$XMLPATCH <<-EOF
		<?xml version="1.0"?>
		<changes>
			<change file="$OUTPUT">
				<add sel="//body">
					<h2>$(sed -e '2,$d' desc.txt)</h2>
					$(sed -e '1,2d' desc.txt | ${QUOTE})
					<table current="true"/>
				</add>
				<add sel="//table[@current='true']">
					<tr>
						<td colspan="2"></td>
						<th>Original file</th>
					</tr>
					<tr>
						<td colspan="2"></td>
						<td><pre><code class="language-xml">$(${QUOTE} orig.xml)</code></pre></td>
					</tr>
					<tr><td/></tr>
					<tr>
						<td></td>
						<th>Patch file</th>
						<th>Result</th>
					</tr>
				</add>
			</change>
		</changes>
	EOF

	for PATCH_FILE in ??_patch.xml; do
		$XMLPATCH <<-EOF
			<?xml version="1.0"?>
			<changes>
				<change file="$OUTPUT">
					<add sel="//table[@current='true']">
						<tr>
							<td>
								<h3>$(sed -e '2,$d' ${PATCH_FILE%_patch.xml}_desc.txt)</h3>
								$(sed -e '1,2d' ${PATCH_FILE%_patch.xml}_desc.txt | ${QUOTE})
							</td>
							<td><pre><code class="language-xml">$(${QUOTE} ${PATCH_FILE})</code></pre></td>
							<td><pre><code class="language-xml">$(${QUOTE} ${PATCH_FILE%_patch.xml}_result.xml)</code></pre></td>
						</tr>
					</add>
				</change>
			</changes>
		EOF
	done

	$XMLPATCH <<-EOF
		<?xml version="1.0"?>
		<changes>
			<change file="$OUTPUT">
				<remove sel="//table[@current='true']/@current"/>
			</change>
		</changes>
	EOF
	cd -
}

prepare
add_dir ../t/adding
add_dir ../t/removing
add_dir ../t/replacing
