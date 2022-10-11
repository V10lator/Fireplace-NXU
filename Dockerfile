FROM wiiuenv/devkitppc:20220917 AS final

ENV PATH=$DEVKITPPC/bin:$PATH \
 WUT_ROOT=$DEVKITPRO/wut

WORKDIR /project
