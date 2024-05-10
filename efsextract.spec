product efsextract
    id "EFS Extract, 0.5"
    image sw
        id "Software"
        version 50
        order 9999
        subsys base default
            id "Software"
            replaces self
            exp efsextract.sw.base
        endsubsys
    endimage
    image man
        id "Man Page"
        version 50
        order 9999
        subsys manpages default
            id "Man Page"
            replaces self
            exp efsextract.man.manpages
        endsubsys
    endimage
endproduct
