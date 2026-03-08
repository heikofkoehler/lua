function f2() return 1 end; function f1() return f2() end; for i=1,10 do f1() end; print('DONE')
