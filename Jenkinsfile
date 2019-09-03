node(label: 'raspberrypi') {
    def dists = ["stretch", "jessie"]
    def srcdir = "${WORKSPACE}/src"

    stage('Checkout') {
        sh "rm -fr ${srcdir}"
        sh "mkdir ${srcdir}"
        dir(srcdir) {
            checkout scm
        }
    }

    for (int i = 0; i < dists.size(); ++i) {
        def dist = dists[i]
        def pkgdir = "package-${dist}"
        def results = "results-${dist}"

        stage("Prepare source for ${dist}") {
            sh "rm -fr ${pkgdir}"
            sh "${srcdir}/prepare-build.sh ${dist} ${pkgdir}"
        }

        stage("Build for ${dist}") {
            sh "rm -fr ${results}"
            sh "mkdir -p ${results}"
            dir(pkgdir) {
                sh "DIST=${dist} pdebuild --use-pdebuild-internal --debbuildopts -b --buildresult ${WORKSPACE}/${results}"
            }
            archiveArtifacts artifacts: "${results}/*.deb", fingerprint: true
        }

        stage("Test install on ${dist}") {
            sh "/build/repo/validate-packages.sh ${dist} ${results}/beast-splitter_*.deb"
        }
    }

    stage('Deploy to internal repository') {
        for (int i = 0; i < dists.size(); ++i) {
            def dist = dists[i]
            def results = "results-${dist}"
            sh "/build/repo/deploy.sh -distribution ${dist} -branch ${env.BRANCH_NAME} ${results}/*.deb"
        }
    }
}
