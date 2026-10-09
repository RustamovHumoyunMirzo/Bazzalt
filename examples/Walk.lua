local B = Bazzalt
local Walk = COMPONENT("Walk")
Walk.Speed = PROPERTY("float", 4.0)

function Walk:OnUpdate(deltaTime)
    local direction = B.Vec3.new(B.Input.GetAxis(B.InputAxis.Horizontal), 0,
                                -B.Input.GetAxis(B.InputAxis.Vertical))
    if direction:LengthSquared() > 0 then
        local transform = self.Entity:GetWorldTransform()
        transform.Position = transform.Position + direction:Normalized() * self.Speed * deltaTime
        self.Entity:SetWorldTransform(transform)
    end
end

return Walk
